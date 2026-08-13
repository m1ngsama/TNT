#include "ratelimit.h"
#include "config_defaults.h"
#include "common.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define RATE_LIMIT_WINDOW 60   /* seconds */
#define MAX_AUTH_FAILURES 5    /* auth failures before block */
#define BLOCK_DURATION 300     /* seconds to block after too many failures */
#define MIN_RATE_LIMIT_RESERVE 256 /* Inactive/security ledgers beyond sessions */

typedef struct {
    char ip[INET6_ADDRSTRLEN];
    time_t window_start;
    int recent_connection_count;
    int active_connections;
    int auth_failure_count;
    bool is_blocked;
    time_t block_until;
} ip_rate_limit_t;

static ip_rate_limit_t *g_rate_limits = NULL;
static int g_rate_limit_capacity = 0;
static pthread_mutex_t g_rate_limit_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_total_connections = 0;
static pthread_mutex_t g_conn_count_lock = PTHREAD_MUTEX_INITIALIZER;

static int g_max_connections = TNT_DEFAULT_MAX_CONNECTIONS;
static int g_max_conn_per_ip = TNT_DEFAULT_MAX_CONN_PER_IP;
static int g_max_conn_rate_per_ip = TNT_DEFAULT_MAX_CONN_RATE_PER_IP;
static int g_rate_limit_enabled = TNT_DEFAULT_RATE_LIMIT_ENABLED;

void ratelimit_init(void) {
    g_max_connections =
        tnt_config_env_int(&TNT_CONFIG_MAX_CONNECTIONS);
    g_max_conn_per_ip =
        tnt_config_env_int(&TNT_CONFIG_MAX_CONN_PER_IP);
    g_max_conn_rate_per_ip =
        tnt_config_env_int(&TNT_CONFIG_MAX_CONN_RATE_PER_IP);
    g_rate_limit_enabled =
        tnt_config_env_int(&TNT_CONFIG_RATE_LIMIT);

    /* Active sessions must never displace each other's per-IP counters.  Keep
     * a separate bounded reserve for connection-rate and authentication
     * ledgers, preserving at least the legacy 256-source security horizon. */
    int target_capacity = g_max_connections + MIN_RATE_LIMIT_RESERVE;

    pthread_mutex_lock(&g_rate_limit_lock);
    if (g_rate_limit_capacity < target_capacity) {
        ip_rate_limit_t *grown = realloc(
            g_rate_limits,
            (size_t)target_capacity * sizeof(*g_rate_limits));
        if (grown) {
            memset(grown + g_rate_limit_capacity, 0,
                   (size_t)(target_capacity - g_rate_limit_capacity) *
                       sizeof(*g_rate_limits));
            g_rate_limits = grown;
            g_rate_limit_capacity = target_capacity;
        } else {
            fprintf(stderr,
                    "Warning: could not grow rate-limit table to %d entries\n",
                    target_capacity);
        }
    }
    pthread_mutex_unlock(&g_rate_limit_lock);
}

/* Caller MUST hold g_rate_limit_lock. */
static ip_rate_limit_t* get_rate_limit_entry(const char *ip) {
    if (!ip || ip[0] == '\0' || !g_rate_limits ||
        g_rate_limit_capacity <= 0) {
        return NULL;
    }

    /* Look for existing entry */
    for (int i = 0; i < g_rate_limit_capacity; i++) {
        if (strcmp(g_rate_limits[i].ip, ip) == 0) {
            return &g_rate_limits[i];
        }
    }

    /* Find empty slot */
    for (int i = 0; i < g_rate_limit_capacity; i++) {
        if (g_rate_limits[i].ip[0] == '\0') {
            strncpy(g_rate_limits[i].ip, ip, sizeof(g_rate_limits[i].ip) - 1);
            g_rate_limits[i].window_start = time(NULL);
            g_rate_limits[i].recent_connection_count = 0;
            g_rate_limits[i].active_connections = 0;
            g_rate_limits[i].auth_failure_count = 0;
            g_rate_limits[i].is_blocked = false;
            g_rate_limits[i].block_until = 0;
            return &g_rate_limits[i];
        }
    }

    /* Reuse the oldest inactive, unblocked entry first.  An unexpired block
     * is security state, not disposable cache data: source churn must not
     * turn a five-minute ban into an immediate retry. */
    int oldest_idx = -1;
    time_t oldest_time = 0;
    time_t now = time(NULL);
    for (int i = 0; i < g_rate_limit_capacity; i++) {
        if (g_rate_limits[i].active_connections != 0 ||
            (g_rate_limits[i].is_blocked &&
             now < g_rate_limits[i].block_until)) {
            continue;
        }
        if (oldest_idx < 0 || g_rate_limits[i].window_start < oldest_time) {
            oldest_time = g_rate_limits[i].window_start;
            oldest_idx = i;
        }
    }

    if (oldest_idx < 0) {
        /* Never evict active accounting or a live block.  Exhaustion under a
         * distributed attack fails closed instead of silently forgiving a
         * source that is still inside its block interval. */
        return NULL;
    }

    /* Reset and reuse */
    strncpy(g_rate_limits[oldest_idx].ip, ip, sizeof(g_rate_limits[oldest_idx].ip) - 1);
    g_rate_limits[oldest_idx].ip[sizeof(g_rate_limits[oldest_idx].ip) - 1] = '\0';
    g_rate_limits[oldest_idx].window_start = time(NULL);
    g_rate_limits[oldest_idx].recent_connection_count = 0;
    g_rate_limits[oldest_idx].active_connections = 0;
    g_rate_limits[oldest_idx].auth_failure_count = 0;
    g_rate_limits[oldest_idx].is_blocked = false;
    g_rate_limits[oldest_idx].block_until = 0;
    return &g_rate_limits[oldest_idx];
}

bool ratelimit_check_ip(const char *ip) {
    time_t now = time(NULL);

    pthread_mutex_lock(&g_rate_limit_lock);
    ip_rate_limit_t *entry = get_rate_limit_entry(ip);
    if (!entry) {
        pthread_mutex_unlock(&g_rate_limit_lock);
        fprintf(stderr, "Rate-limit table unavailable for %s\n",
                ip ? ip : "unknown");
        return false;
    }

    if (entry->active_connections >= g_max_conn_per_ip) {
        pthread_mutex_unlock(&g_rate_limit_lock);
        fprintf(stderr, "Concurrent IP limit reached for %s\n", ip);
        return false;
    }

    if (g_rate_limit_enabled && entry->is_blocked && now < entry->block_until) {
        pthread_mutex_unlock(&g_rate_limit_lock);
        fprintf(stderr, "Blocked IP %s (blocked until %ld)\n", ip, (long)entry->block_until);
        return false;
    }

    if (g_rate_limit_enabled && entry->is_blocked && now >= entry->block_until) {
        entry->is_blocked = false;
        entry->auth_failure_count = 0;
    }

    if (g_rate_limit_enabled) {
        if (now - entry->window_start >= RATE_LIMIT_WINDOW) {
            entry->window_start = now;
            entry->recent_connection_count = 0;
        }

        entry->recent_connection_count++;
        if (entry->recent_connection_count > g_max_conn_rate_per_ip) {
            entry->is_blocked = true;
            entry->block_until = now + BLOCK_DURATION;
            pthread_mutex_unlock(&g_rate_limit_lock);
            fprintf(stderr, "Rate limit exceeded for IP %s\n", ip);
            return false;
        }
    }

    entry->active_connections++;
    pthread_mutex_unlock(&g_rate_limit_lock);
    return true;
}

void ratelimit_record_auth_failure(const char *ip) {
    time_t now = time(NULL);

    if (!g_rate_limit_enabled) {
        return;
    }

    pthread_mutex_lock(&g_rate_limit_lock);
    ip_rate_limit_t *entry = get_rate_limit_entry(ip);
    if (!entry) {
        pthread_mutex_unlock(&g_rate_limit_lock);
        return;
    }

    entry->auth_failure_count++;
    if (entry->auth_failure_count >= MAX_AUTH_FAILURES) {
        entry->is_blocked = true;
        entry->block_until = now + BLOCK_DURATION;
        fprintf(stderr, "IP %s blocked due to %d auth failures\n", ip, entry->auth_failure_count);
    }

    pthread_mutex_unlock(&g_rate_limit_lock);
}

void ratelimit_record_auth_success(const char *ip) {
    if (!ip || ip[0] == '\0') return;

    pthread_mutex_lock(&g_rate_limit_lock);
    ip_rate_limit_t *entry = get_rate_limit_entry(ip);
    if (entry) {
        entry->auth_failure_count = 0;
    }
    pthread_mutex_unlock(&g_rate_limit_lock);
}

void ratelimit_release_ip(const char *ip) {
    if (!ip || ip[0] == '\0') {
        return;
    }

    pthread_mutex_lock(&g_rate_limit_lock);
    /* Release must never manufacture a new ledger.  Apart from polluting the
     * bounded table, doing so could recycle unrelated inactive state after a
     * caller passes an address whose admission was never recorded. */
    for (int i = 0; i < g_rate_limit_capacity; i++) {
        if (strcmp(g_rate_limits[i].ip, ip) == 0) {
            if (g_rate_limits[i].active_connections > 0) {
                g_rate_limits[i].active_connections--;
            }
            break;
        }
    }
    pthread_mutex_unlock(&g_rate_limit_lock);
}

bool ratelimit_check_and_increment_total(void) {
    pthread_mutex_lock(&g_conn_count_lock);

    if (g_total_connections >= g_max_connections) {
        pthread_mutex_unlock(&g_conn_count_lock);
        return false;
    }

    g_total_connections++;
    pthread_mutex_unlock(&g_conn_count_lock);
    return true;
}

void ratelimit_decrement_total(void) {
    pthread_mutex_lock(&g_conn_count_lock);
    if (g_total_connections > 0) {
        g_total_connections--;
    }
    pthread_mutex_unlock(&g_conn_count_lock);
}

int ratelimit_get_active_total(void) {
    int count;
    pthread_mutex_lock(&g_conn_count_lock);
    count = g_total_connections;
    pthread_mutex_unlock(&g_conn_count_lock);
    return count;
}

#ifdef TNT_TESTING
int ratelimit_test_capacity(void) {
    int capacity;
    pthread_mutex_lock(&g_rate_limit_lock);
    capacity = g_rate_limit_capacity;
    pthread_mutex_unlock(&g_rate_limit_lock);
    return capacity;
}

int ratelimit_test_active_for_ip(const char *ip) {
    int active = -1;
    pthread_mutex_lock(&g_rate_limit_lock);
    for (int i = 0; i < g_rate_limit_capacity; i++) {
        if (ip && strcmp(g_rate_limits[i].ip, ip) == 0) {
            active = g_rate_limits[i].active_connections;
            break;
        }
    }
    pthread_mutex_unlock(&g_rate_limit_lock);
    return active;
}

int ratelimit_test_auth_failures_for_ip(const char *ip) {
    int failures = -1;
    pthread_mutex_lock(&g_rate_limit_lock);
    for (int i = 0; g_rate_limits && i < g_rate_limit_capacity; i++) {
        if (ip && strcmp(g_rate_limits[i].ip, ip) == 0) {
            failures = g_rate_limits[i].auth_failure_count;
            break;
        }
    }
    pthread_mutex_unlock(&g_rate_limit_lock);
    return failures;
}
#endif
