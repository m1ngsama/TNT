#include "ratelimit.h"
#include "common.h"
#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MAX_TRACKED_IPS 256
#define RATE_LIMIT_WINDOW 60   /* seconds */
#define MAX_AUTH_FAILURES 5    /* auth failures before block */
#define BLOCK_DURATION 300     /* seconds to block after too many failures */

typedef struct {
    char ip[INET6_ADDRSTRLEN];
    time_t window_start;
    int recent_connection_count;
    int active_connections;
    int auth_failure_count;
    bool is_blocked;
    time_t block_until;
} ip_rate_limit_t;

static ip_rate_limit_t g_rate_limits[MAX_TRACKED_IPS];
static pthread_mutex_t g_rate_limit_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_total_connections = 0;
static pthread_mutex_t g_conn_count_lock = PTHREAD_MUTEX_INITIALIZER;

static int g_max_connections = 64;
static int g_max_conn_per_ip = 5;
static int g_max_conn_rate_per_ip = 10;
static int g_rate_limit_enabled = 1;

void ratelimit_init(void) {
    g_max_connections        = env_int("TNT_MAX_CONNECTIONS",       64, 1, 1024);
    g_max_conn_per_ip        = env_int("TNT_MAX_CONN_PER_IP",        5, 1, 1024);
    g_max_conn_rate_per_ip   = env_int("TNT_MAX_CONN_RATE_PER_IP",  10, 1, 1024);
    g_rate_limit_enabled     = env_int("TNT_RATE_LIMIT",             1, 0,    1);
}

/* Caller MUST hold g_rate_limit_lock. */
static ip_rate_limit_t* get_rate_limit_entry(const char *ip) {
    /* Look for existing entry */
    for (int i = 0; i < MAX_TRACKED_IPS; i++) {
        if (strcmp(g_rate_limits[i].ip, ip) == 0) {
            return &g_rate_limits[i];
        }
    }

    /* Find empty slot */
    for (int i = 0; i < MAX_TRACKED_IPS; i++) {
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

    /* Reuse the oldest inactive entry first so active IP accounting stays intact. */
    int oldest_idx = -1;
    time_t oldest_time = 0;
    for (int i = 0; i < MAX_TRACKED_IPS; i++) {
        if (g_rate_limits[i].active_connections != 0) {
            continue;
        }
        if (oldest_idx < 0 || g_rate_limits[i].window_start < oldest_time) {
            oldest_time = g_rate_limits[i].window_start;
            oldest_idx = i;
        }
    }

    if (oldest_idx < 0) {
        /* All slots have active connections — evicting will corrupt their
         * concurrency accounting.  Pick the oldest entry but warn. */
        oldest_idx = 0;
        oldest_time = g_rate_limits[0].window_start;
        for (int i = 1; i < MAX_TRACKED_IPS; i++) {
            if (g_rate_limits[i].window_start < oldest_time) {
                oldest_time = g_rate_limits[i].window_start;
                oldest_idx = i;
            }
        }
        fprintf(stderr, "Warning: rate-limit table full, evicting active IP %s "
                "(%d active connections lost)\n",
                g_rate_limits[oldest_idx].ip,
                g_rate_limits[oldest_idx].active_connections);
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
        if (entry->recent_connection_count >= g_max_conn_rate_per_ip) {
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

    entry->auth_failure_count++;
    if (entry->auth_failure_count >= MAX_AUTH_FAILURES) {
        entry->is_blocked = true;
        entry->block_until = now + BLOCK_DURATION;
        fprintf(stderr, "IP %s blocked due to %d auth failures\n", ip, entry->auth_failure_count);
    }

    pthread_mutex_unlock(&g_rate_limit_lock);
}

void ratelimit_release_ip(const char *ip) {
    if (!ip || ip[0] == '\0') {
        return;
    }

    pthread_mutex_lock(&g_rate_limit_lock);
    ip_rate_limit_t *entry = get_rate_limit_entry(ip);
    if (entry->active_connections > 0) {
        entry->active_connections--;
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
