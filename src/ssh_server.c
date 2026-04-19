#include "ssh_server.h"
#include "tui.h"
#include "utf8.h"
#include <libssh/libssh.h>
#include <libssh/server.h>
#include <libssh/callbacks.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <stdarg.h>
#include <sys/stat.h>

/* Global SSH bind instance */
static ssh_bind g_sshbind = NULL;
static int g_listen_port = DEFAULT_PORT;

/* Session context for callback-based API */
typedef struct {
    char client_ip[INET6_ADDRSTRLEN];
    char requested_user[MAX_USERNAME_LEN];
    int pty_width;
    int pty_height;
    char exec_command[MAX_EXEC_COMMAND_LEN];
    bool auth_success;
    int auth_attempts;
    bool channel_ready;  /* Set when shell/exec request received */
    ssh_channel channel;  /* Channel created in callback */
    struct ssh_channel_callbacks_struct *channel_cb;  /* Channel callbacks */
} session_context_t;

typedef struct {
    ssh_session session;
    char client_ip[INET6_ADDRSTRLEN];
} accepted_session_t;

/* Rate limiting and connection tracking */
#define MAX_TRACKED_IPS 256
#define RATE_LIMIT_WINDOW 60        /* seconds */
#define MAX_AUTH_FAILURES 5         /* auth failures before block */
#define BLOCK_DURATION 300          /* seconds to block after too many failures */

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
static time_t g_server_start_time = 0;

/* Configuration from environment variables */
static int g_max_connections = 64;
static int g_max_conn_per_ip = 5;
static int g_max_conn_rate_per_ip = 10;
static int g_rate_limit_enabled = 1;
static char g_access_token[256] = "";

static void buffer_appendf(char *buffer, size_t buf_size, size_t *pos,
                           const char *fmt, ...) {
    va_list args;
    int written;

    if (!buffer || !pos || !fmt || buf_size == 0 || *pos >= buf_size - 1) {
        return;
    }

    va_start(args, fmt);
    written = vsnprintf(buffer + *pos, buf_size - *pos, fmt, args);
    va_end(args);

    if (written < 0) {
        return;
    }

    if ((size_t)written >= buf_size - *pos) {
        *pos = buf_size - 1;
    } else {
        *pos += (size_t)written;
    }
}

static void buffer_append_bytes(char *buffer, size_t buf_size, size_t *pos,
                                const char *data, size_t len) {
    size_t available;
    size_t to_copy;

    if (!buffer || !pos || !data || len == 0 || buf_size == 0 ||
        *pos >= buf_size - 1) {
        return;
    }

    available = (buf_size - 1) - *pos;
    to_copy = (len < available) ? len : available;
    memcpy(buffer + *pos, data, to_copy);
    *pos += to_copy;
    buffer[*pos] = '\0';
}

/* Constant-time string comparison to prevent timing side-channel attacks.
 * Always iterates over the full length of the secret (b) to avoid leaking
 * its length.  When the input (a) is shorter, compares against zero bytes;
 * the length mismatch is folded into the result separately. */
static bool constant_time_strcmp(const char *a, const char *b) {
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    volatile unsigned char result = (unsigned char)(len_a ^ len_b);
    for (size_t i = 0; i < len_b; i++) {
        unsigned char ca = (i < len_a) ? (unsigned char)a[i] : 0;
        result |= ca ^ (unsigned char)b[i];
    }
    return result == 0;
}

/* Safe integer parse from environment variable; returns fallback on error. */
static int env_int(const char *name, int fallback, int min_val, int max_val) {
    const char *env = getenv(name);
    if (!env || env[0] == '\0') return fallback;
    char *end;
    long val = strtol(env, &end, 10);
    if (*end != '\0' || val < min_val || val > max_val) return fallback;
    return (int)val;
}

/* Initialize rate limit configuration from environment */
static void init_rate_limit_config(void) {
    const char *env;

    g_max_connections = env_int("TNT_MAX_CONNECTIONS", 64, 1, 1024);
    g_max_conn_per_ip = env_int("TNT_MAX_CONN_PER_IP", 5, 1, 1024);
    g_max_conn_rate_per_ip = env_int("TNT_MAX_CONN_RATE_PER_IP", 10, 1, 1024);
    g_rate_limit_enabled = env_int("TNT_RATE_LIMIT", 1, 0, 1);

    if ((env = getenv("TNT_ACCESS_TOKEN")) != NULL) {
        strncpy(g_access_token, env, sizeof(g_access_token) - 1);
        g_access_token[sizeof(g_access_token) - 1] = '\0';
    }
}

/* Get or create rate limit entry for an IP */
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

/* Check rate and concurrency limits for an IP */
static bool check_ip_connection_policy(const char *ip) {
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

/* Record authentication failure */
static void record_auth_failure(const char *ip) {
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

static void release_ip_connection(const char *ip) {
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

/* Check and increment total connection count */
static bool check_and_increment_connections(void) {
    pthread_mutex_lock(&g_conn_count_lock);

    if (g_total_connections >= g_max_connections) {
        pthread_mutex_unlock(&g_conn_count_lock);
        return false;
    }

    g_total_connections++;
    pthread_mutex_unlock(&g_conn_count_lock);
    return true;
}

/* Decrement connection count */
static void decrement_connections(void) {
    pthread_mutex_lock(&g_conn_count_lock);
    if (g_total_connections > 0) {
        g_total_connections--;
    }
    pthread_mutex_unlock(&g_conn_count_lock);
}

/* Get client IP address */
static void get_client_ip(ssh_session session, char *ip_buf, size_t buf_size) {
    int fd = ssh_get_fd(session);
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);

    if (getpeername(fd, (struct sockaddr *)&addr, &addr_len) == 0) {
        if (addr.ss_family == AF_INET) {
            struct sockaddr_in *s = (struct sockaddr_in *)&addr;
            inet_ntop(AF_INET, &s->sin_addr, ip_buf, buf_size);
        } else if (addr.ss_family == AF_INET6) {
            struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;
            inet_ntop(AF_INET6, &s->sin6_addr, ip_buf, buf_size);
        } else {
            strncpy(ip_buf, "unknown", buf_size - 1);
        }
    } else {
        strncpy(ip_buf, "unknown", buf_size - 1);
    }
    ip_buf[buf_size - 1] = '\0';
}

static void sanitize_terminal_size(int *width, int *height) {
    if (!width || !height) {
        return;
    }

    if (*width <= 0 || *width > 500) {
        *width = 80;
    }
    if (*height <= 0 || *height > 200) {
        *height = 24;
    }
}

static void format_timestamp_utc(time_t ts, char *buffer, size_t buf_size) {
    struct tm tm_info;

    if (!buffer || buf_size == 0) {
        return;
    }

    gmtime_r(&ts, &tm_info);
    strftime(buffer, buf_size, "%Y-%m-%dT%H:%M:%SZ", &tm_info);
}

/* Validate username to prevent injection attacks */
static bool is_valid_username(const char *username) {
    if (!username || username[0] == '\0') {
        return false;
    }

    /* Reject usernames starting with special characters */
    if (username[0] == ' ' || username[0] == '.' || username[0] == '-') {
        return false;
    }

    /* Check for illegal characters that could cause injection */
    const char *illegal_chars = "|;&$`\n\r<>(){}[]'\"\\";
    for (size_t i = 0; i < strlen(username); i++) {
        /* Reject control characters (except tab) */
        if (username[i] < 32 && username[i] != 9) {
            return false;
        }
        /* Reject shell metacharacters */
        if (strchr(illegal_chars, username[i])) {
            return false;
        }
    }

    return true;
}
/* Generate or load SSH host key */
static int setup_host_key(ssh_bind sshbind) {
    struct stat st;
    char host_key_path[PATH_MAX];

    if (tnt_state_path(host_key_path, sizeof(host_key_path), HOST_KEY_FILE) < 0) {
        fprintf(stderr, "State directory path is too long\n");
        return -1;
    }

    /* Check if host key exists */
    if (stat(host_key_path, &st) == 0) {
        /* Validate file size */
        if (st.st_size == 0) {
            fprintf(stderr, "Warning: Empty key file, regenerating...\n");
            unlink(host_key_path);
            /* Fall through to generate new key */
        } else if (st.st_size > 10 * 1024 * 1024) {
            /* Sanity check: key file shouldn't be > 10MB */
            fprintf(stderr, "Error: Key file too large (%lld bytes)\n", (long long)st.st_size);
            return -1;
        } else {
            /* Verify and fix permissions */
            if ((st.st_mode & 0077) != 0) {
                fprintf(stderr, "Warning: Fixing insecure key file permissions\n");
                chmod(host_key_path, 0600);
            }

            /* Load existing key */
            if (ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_RSAKEY, host_key_path) < 0) {
                fprintf(stderr, "Failed to load host key: %s\n", ssh_get_error(sshbind));
                return -1;
            }
            return 0;
        }
    }

    /* Generate new key */
    printf("Generating new RSA 4096-bit host key...\n");
    ssh_key key;
    if (ssh_pki_generate(SSH_KEYTYPE_RSA, 4096, &key) < 0) {
        fprintf(stderr, "Failed to generate RSA key\n");
        return -1;
    }

    /* Create temporary file with secure permissions (atomic operation) */
    char temp_key_file[PATH_MAX];
    if (snprintf(temp_key_file, sizeof(temp_key_file), "%s.tmp.%d",
                 host_key_path, getpid()) >= (int)sizeof(temp_key_file)) {
        fprintf(stderr, "Temporary key path is too long\n");
        ssh_key_free(key);
        return -1;
    }

    /* Set umask to ensure restrictive permissions before file creation */
    mode_t old_umask = umask(0077);

    /* Export key to temporary file */
    if (ssh_pki_export_privkey_file(key, NULL, NULL, NULL, temp_key_file) < 0) {
        fprintf(stderr, "Failed to export host key\n");
        ssh_key_free(key);
        umask(old_umask);
        return -1;
    }

    ssh_key_free(key);

    /* Restore original umask */
    umask(old_umask);

    /* Ensure restrictive permissions */
    chmod(temp_key_file, 0600);

    /* Atomically replace the old key file (if any) */
    if (rename(temp_key_file, host_key_path) < 0) {
        fprintf(stderr, "Failed to rename temporary key file\n");
        unlink(temp_key_file);
        return -1;
    }

    /* Load the newly created key */
    if (ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_RSAKEY, host_key_path) < 0) {
        fprintf(stderr, "Failed to load host key: %s\n", ssh_get_error(sshbind));
        return -1;
    }

    return 0;
}

/* Send data to client via SSH channel */
int client_send(client_t *client, const char *data, size_t len) {
    size_t total = 0;

    if (!client || !data) return -1;

    pthread_mutex_lock(&client->io_lock);

    if (!client->connected || !client->channel) {
        pthread_mutex_unlock(&client->io_lock);
        return -1;
    }

    while (total < len) {
        size_t remaining = len - total;
        uint32_t chunk = (remaining > 32768) ? 32768 : (uint32_t)remaining;
        int sent = ssh_channel_write(client->channel, data + total, chunk);
        if (sent <= 0) {
            pthread_mutex_unlock(&client->io_lock);
            return -1;
        }
        total += (size_t)sent;
    }

    pthread_mutex_unlock(&client->io_lock);
    return 0;
}

void client_addref(client_t *client) {
    if (!client) return;
    pthread_mutex_lock(&client->ref_lock);
    client->ref_count++;
    pthread_mutex_unlock(&client->ref_lock);
}

void client_release(client_t *client) {
    if (!client) return;

    pthread_mutex_lock(&client->ref_lock);
    client->ref_count--;
    int count = client->ref_count;
    pthread_mutex_unlock(&client->ref_lock);

    if (count == 0) {
        /* Safe to free now */
        if (client->channel && client->channel_cb) {
            ssh_remove_channel_callbacks(client->channel, client->channel_cb);
        }
        if (client->channel) {
            ssh_channel_close(client->channel);
            ssh_channel_free(client->channel);
        }
        if (client->session) {
            ssh_disconnect(client->session);
            ssh_free(client->session);
        }
        if (client->channel_cb) {
            free(client->channel_cb);
        }
        pthread_mutex_destroy(&client->io_lock);
        pthread_mutex_destroy(&client->ref_lock);
        free(client);
    }
}

/* Send formatted string to client */
int client_printf(client_t *client, const char *fmt, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    /* Check for buffer overflow or encoding error */
    if (len < 0 || len >= (int)sizeof(buffer)) {
        return -1;
    }

    return client_send(client, buffer, len);
}

/* Read username from client */
static int read_username(client_t *client) {
    char username[MAX_USERNAME_LEN] = {0};
    int pos = 0;
    char buf[4];

    tui_clear_screen(client);
    client_printf(client, "================================\r\n");
    client_printf(client, "  欢迎来到 TNT 匿名聊天室\r\n");
    client_printf(client, "  Welcome to TNT Anonymous Chat\r\n");
    client_printf(client, "================================\r\n\r\n");
    client_printf(client, "请输入用户名 (留空默认为 anonymous): ");

    while (1) {
        int n = ssh_channel_read_timeout(client->channel, buf, 1, 0, 60000); /* 60 sec timeout */

        if (n == SSH_AGAIN) {
            /* Timeout */
            if (!ssh_channel_is_open(client->channel)) {
                return -1;
            }
            continue;
        }

        if (n <= 0) return -1;

        unsigned char b = buf[0];

        if (b == '\r' || b == '\n') {
            break;
        } else if (b == 127 || b == 8) {  /* Backspace */
            if (pos > 0) {
                utf8_remove_last_char(username);
                pos = strlen(username);
                client_printf(client, "\b \b");
            }
        } else if (b < 32) {
            /* Ignore control characters */
        } else if (b < 128) {
            /* ASCII */
            if (pos < MAX_USERNAME_LEN - 1) {
                username[pos++] = b;
                username[pos] = '\0';
                client_send(client, (char *)&b, 1);
            }
        } else {
            /* UTF-8 multi-byte */
            int len = utf8_byte_length(b);
            if (len <= 0 || len > 4) {
                /* Invalid UTF-8 start byte */
                continue;
            }
            buf[0] = b;
            if (len > 1) {
                int read_bytes = ssh_channel_read_timeout(client->channel, &buf[1], len - 1, 0, 5000);
                if (read_bytes != len - 1) {
                    /* Incomplete or timed-out UTF-8 continuation */
                    continue;
                }
            }
            /* Validate the complete UTF-8 sequence */
            if (!utf8_is_valid_sequence(buf, len)) {
                continue;
            }
            if (pos + len < MAX_USERNAME_LEN - 1) {
                memcpy(username + pos, buf, len);
                pos += len;
                username[pos] = '\0';
                client_send(client, buf, len);
            }
        }
    }

    client_printf(client, "\r\n");

    if (username[0] == '\0') {
        strncpy(client->username, "anonymous", MAX_USERNAME_LEN - 1);
        client->username[MAX_USERNAME_LEN - 1] = '\0';
    } else {
        strncpy(client->username, username, MAX_USERNAME_LEN - 1);
        client->username[MAX_USERNAME_LEN - 1] = '\0';

        /* Validate username for security */
        if (!is_valid_username(client->username)) {
            client_printf(client, "Invalid username. Using 'anonymous' instead.\r\n");
            strcpy(client->username, "anonymous");
        } else {
            /* Truncate to 20 characters */
            if (utf8_strlen(client->username) > 20) {
                utf8_truncate(client->username, 20);
            }
        }
    }

    return 0;
}

static void trim_ascii_whitespace(char *text) {
    char *start;
    char *end;

    if (!text || text[0] == '\0') {
        return;
    }

    start = text;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }

    if (text[0] == '\0') {
        return;
    }

    end = text + strlen(text) - 1;
    while (end >= text && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
}

static void json_append_string(char *buffer, size_t buf_size, size_t *pos,
                               const char *text) {
    const unsigned char *p = (const unsigned char *)(text ? text : "");

    buffer_append_bytes(buffer, buf_size, pos, "\"", 1);

    while (*p && *pos < buf_size - 1) {
        char escaped[7];

        switch (*p) {
            case '\\':
                buffer_append_bytes(buffer, buf_size, pos, "\\\\", 2);
                break;
            case '"':
                buffer_append_bytes(buffer, buf_size, pos, "\\\"", 2);
                break;
            case '\n':
                buffer_append_bytes(buffer, buf_size, pos, "\\n", 2);
                break;
            case '\r':
                buffer_append_bytes(buffer, buf_size, pos, "\\r", 2);
                break;
            case '\t':
                buffer_append_bytes(buffer, buf_size, pos, "\\t", 2);
                break;
            default:
                if (*p < 0x20) {
                    snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
                    buffer_append_bytes(buffer, buf_size, pos,
                                        escaped, strlen(escaped));
                } else {
                    buffer_append_bytes(buffer, buf_size, pos,
                                        (const char *)p, 1);
                }
                break;
        }
        p++;
    }

    buffer_append_bytes(buffer, buf_size, pos, "\"", 1);
}

static void resolve_exec_username(const client_t *client, char *buffer,
                                  size_t buf_size) {
    if (!buffer || buf_size == 0) {
        return;
    }

    if (client && client->ssh_login[0] != '\0' &&
        is_valid_username(client->ssh_login)) {
        snprintf(buffer, buf_size, "%s", client->ssh_login);
    } else {
        snprintf(buffer, buf_size, "%s", "anonymous");
    }

    if (utf8_strlen(buffer) > 20) {
        utf8_truncate(buffer, 20);
    }
}

static int exec_command_help(client_t *client) {
    static const char help_text[] =
        "TNT exec interface\n"
        "Commands:\n"
        "  help            Show this help\n"
        "  health          Print service health\n"
        "  users [--json]  List online users\n"
        "  stats [--json]  Print room statistics\n"
        "  tail [N]        Print recent messages\n"
        "  tail -n N       Print recent messages\n"
        "  post MESSAGE    Post a message non-interactively\n"
        "  exit            Exit successfully\n";

    return client_send(client, help_text, sizeof(help_text) - 1) == 0 ? 0 : 1;
}

static int exec_command_health(client_t *client) {
    static const char ok[] = "ok\n";
    return client_send(client, ok, sizeof(ok) - 1) == 0 ? 0 : 1;
}

static int exec_command_users(client_t *client, bool json) {
    int count;
    char (*usernames)[MAX_USERNAME_LEN] = NULL;
    char *output;
    size_t output_size;
    size_t pos = 0;
    int rc;

    pthread_rwlock_rdlock(&g_room->lock);
    count = g_room->client_count;
    if (count > 0) {
        usernames = calloc((size_t)count, sizeof(*usernames));
        if (!usernames) {
            pthread_rwlock_unlock(&g_room->lock);
            client_printf(client, "users: out of memory\n");
            return 1;
        }

        for (int i = 0; i < count; i++) {
            snprintf(usernames[i], MAX_USERNAME_LEN, "%s",
                     g_room->clients[i]->username);
        }
    }
    pthread_rwlock_unlock(&g_room->lock);

    output_size = json ? ((size_t)count * (MAX_USERNAME_LEN * 2 + 8) + 8)
                       : ((size_t)count * (MAX_USERNAME_LEN + 1) + 1);
    if (output_size < 8) {
        output_size = 8;
    }

    output = calloc(output_size, 1);
    if (!output) {
        free(usernames);
        client_printf(client, "users: out of memory\n");
        return 1;
    }

    if (json) {
        buffer_append_bytes(output, output_size, &pos, "[", 1);
        for (int i = 0; i < count; i++) {
            if (i > 0) {
                buffer_append_bytes(output, output_size, &pos, ",", 1);
            }
            json_append_string(output, output_size, &pos, usernames[i]);
        }
        buffer_append_bytes(output, output_size, &pos, "]\n", 2);
    } else {
        for (int i = 0; i < count; i++) {
            buffer_appendf(output, output_size, &pos, "%s\n", usernames[i]);
        }
    }

    rc = client_send(client, output, pos) == 0 ? 0 : 1;
    free(output);
    free(usernames);
    return rc;
}

static int exec_command_stats(client_t *client, bool json) {
    int online_users;
    int message_count;
    int client_capacity;
    int active_connections;
    time_t now = time(NULL);
    long uptime_seconds;
    char buffer[512];
    int len;

    pthread_rwlock_rdlock(&g_room->lock);
    online_users = g_room->client_count;
    message_count = g_room->message_count;
    client_capacity = g_room->client_capacity;
    pthread_rwlock_unlock(&g_room->lock);

    pthread_mutex_lock(&g_conn_count_lock);
    active_connections = g_total_connections;
    pthread_mutex_unlock(&g_conn_count_lock);

    uptime_seconds = (g_server_start_time > 0 && now >= g_server_start_time)
                     ? (long)(now - g_server_start_time)
                     : 0;

    if (json) {
        len = snprintf(buffer, sizeof(buffer),
                       "{\"status\":\"ok\",\"online_users\":%d,"
                       "\"message_count\":%d,\"client_capacity\":%d,"
                       "\"active_connections\":%d,\"uptime_seconds\":%ld}\n",
                       online_users, message_count, client_capacity,
                       active_connections, uptime_seconds);
    } else {
        len = snprintf(buffer, sizeof(buffer),
                       "status ok\n"
                       "online_users %d\n"
                       "message_count %d\n"
                       "client_capacity %d\n"
                       "active_connections %d\n"
                       "uptime_seconds %ld\n",
                       online_users, message_count, client_capacity,
                       active_connections, uptime_seconds);
    }

    if (len < 0 || len >= (int)sizeof(buffer)) {
        client_printf(client, "stats: output overflow\n");
        return 1;
    }

    return client_send(client, buffer, (size_t)len) == 0 ? 0 : 1;
}

static int parse_tail_count(const char *args, int *count) {
    char *end = NULL;
    long value;

    if (!count) {
        return -1;
    }

    *count = 20;
    if (!args || args[0] == '\0') {
        return 0;
    }

    if (strncmp(args, "-n", 2) == 0 && isspace((unsigned char)args[2])) {
        args += 2;
        while (*args && isspace((unsigned char)*args)) {
            args++;
        }
    }

    value = strtol(args, &end, 10);
    if (end == args) {
        return -1;
    }
    while (*end) {
        if (!isspace((unsigned char)*end)) {
            return -1;
        }
        end++;
    }

    if (value < 1 || value > MAX_MESSAGES) {
        return -1;
    }

    *count = (int)value;
    return 0;
}

static int exec_command_tail(client_t *client, const char *args) {
    int requested = 20;
    int total_messages;
    int start;
    int count;
    message_t *snapshot = NULL;
    char *output;
    size_t output_size;
    size_t pos = 0;
    int rc;

    if (parse_tail_count(args, &requested) < 0) {
        client_printf(client, "tail: usage: tail [N] | tail -n N\n");
        return 64;
    }

    pthread_rwlock_rdlock(&g_room->lock);
    total_messages = g_room->message_count;
    start = total_messages - requested;
    if (start < 0) {
        start = 0;
    }
    count = total_messages - start;

    if (count > 0) {
        snapshot = calloc((size_t)count, sizeof(message_t));
        if (!snapshot) {
            pthread_rwlock_unlock(&g_room->lock);
            client_printf(client, "tail: out of memory\n");
            return 1;
        }
        memcpy(snapshot, &g_room->messages[start], (size_t)count * sizeof(message_t));
    }
    pthread_rwlock_unlock(&g_room->lock);

    output_size = (size_t)(count > 0 ? count : 1) *
                  (MAX_USERNAME_LEN + MAX_MESSAGE_LEN + 48);
    output = calloc(output_size, 1);
    if (!output) {
        free(snapshot);
        client_printf(client, "tail: out of memory\n");
        return 1;
    }

    for (int i = 0; i < count; i++) {
        char timestamp[64];
        format_timestamp_utc(snapshot[i].timestamp, timestamp, sizeof(timestamp));
        buffer_appendf(output, output_size, &pos, "%s\t%s\t%s\n",
                       timestamp, snapshot[i].username, snapshot[i].content);
    }

    rc = client_send(client, output, pos) == 0 ? 0 : 1;
    free(output);
    free(snapshot);
    return rc;
}

static int exec_command_post(client_t *client, const char *args) {
    char content[MAX_MESSAGE_LEN];
    char username[MAX_USERNAME_LEN];
    message_t msg = {
        .timestamp = time(NULL),
    };

    if (!args || args[0] == '\0') {
        client_printf(client, "post: usage: post MESSAGE\n");
        return 64;
    }

    strncpy(content, args, sizeof(content) - 1);
    content[sizeof(content) - 1] = '\0';
    trim_ascii_whitespace(content);

    if (content[0] == '\0') {
        client_printf(client, "post: message cannot be empty\n");
        return 64;
    }

    if (!utf8_is_valid_string(content)) {
        client_printf(client, "post: invalid UTF-8 input\n");
        return 1;
    }

    resolve_exec_username(client, username, sizeof(username));

    strncpy(msg.username, username, sizeof(msg.username) - 1);
    msg.username[sizeof(msg.username) - 1] = '\0';
    strncpy(msg.content, content, sizeof(msg.content) - 1);
    msg.content[sizeof(msg.content) - 1] = '\0';

    room_broadcast(g_room, &msg);
    if (message_save(&msg) < 0) {
        client_printf(client, "post: failed to persist message\n");
        return 1;
    }

    return client_send(client, "posted\n", 7) == 0 ? 0 : 1;
}

static int execute_exec_command(client_t *client) {
    char command_copy[MAX_EXEC_COMMAND_LEN];
    char *cmd;
    char *args;

    strncpy(command_copy, client->exec_command, sizeof(command_copy) - 1);
    command_copy[sizeof(command_copy) - 1] = '\0';
    trim_ascii_whitespace(command_copy);

    cmd = command_copy;
    if (*cmd == '\0') {
        return exec_command_help(client);
    }

    args = cmd;
    while (*args && !isspace((unsigned char)*args)) {
        args++;
    }
    if (*args) {
        *args++ = '\0';
        while (*args && isspace((unsigned char)*args)) {
            args++;
        }
    } else {
        args = NULL;
    }

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0) {
        return exec_command_help(client);
    }
    if (strcmp(cmd, "health") == 0) {
        return exec_command_health(client);
    }
    if (strcmp(cmd, "users") == 0) {
        if (args && strcmp(args, "--json") != 0) {
            client_printf(client, "users: usage: users [--json]\n");
            return 64;
        }
        return exec_command_users(client, args != NULL);
    }
    if (strcmp(cmd, "stats") == 0) {
        if (args && strcmp(args, "--json") != 0) {
            client_printf(client, "stats: usage: stats [--json]\n");
            return 64;
        }
        return exec_command_stats(client, args != NULL);
    }
    if (strcmp(cmd, "tail") == 0) {
        return exec_command_tail(client, args);
    }
    if (strcmp(cmd, "post") == 0) {
        return exec_command_post(client, args);
    }
    if (strcmp(cmd, "exit") == 0) {
        return 0;
    }

    client_printf(client, "Unknown command: %s\n", cmd);
    return 64;
}

/* Execute a command */
static void execute_command(client_t *client) {
    char *cmd = client->command_input;
    char output[2048] = {0};
    size_t pos = 0;

    /* Trim whitespace */
    while (*cmd == ' ') cmd++;
    size_t cmd_len = strlen(cmd);
    if (cmd_len > 0) {
        char *end = cmd + cmd_len - 1;
        while (end > cmd && *end == ' ') {
            *end = '\0';
            end--;
        }
    }

    if (strcmp(cmd, "list") == 0 || strcmp(cmd, "users") == 0 ||
        strcmp(cmd, "who") == 0) {
        buffer_appendf(output, sizeof(output), &pos,
                       "========================================\n"
                       "     Online Users / 在线用户\n"
                       "========================================\n");

        pthread_rwlock_rdlock(&g_room->lock);
        buffer_appendf(output, sizeof(output), &pos,
                       "Total / 总数: %d\n"
                       "----------------------------------------\n",
                       g_room->client_count);

        for (int i = 0; i < g_room->client_count; i++) {
            char marker = (g_room->clients[i] == client) ? '*' : ' ';
            buffer_appendf(output, sizeof(output), &pos,
                           "%c %d. %s\n", marker, i + 1,
                           g_room->clients[i]->username);
        }

        pthread_rwlock_unlock(&g_room->lock);

        buffer_appendf(output, sizeof(output), &pos,
                       "========================================\n"
                       "* = you / 你\n");

    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "commands") == 0) {
        buffer_appendf(output, sizeof(output), &pos,
                       "========================================\n"
                       "        Available Commands\n"
                       "========================================\n"
                       "list, users, who - Show online users\n"
                       "help, commands   - Show this help\n"
                       "clear, cls       - Clear command output\n"
                       "========================================\n");

    } else if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0) {
        buffer_appendf(output, sizeof(output), &pos, "Command output cleared\n");

    } else if (cmd[0] == '\0') {
        /* Empty command */
        client->mode = MODE_NORMAL;
        client->command_input[0] = '\0';
        tui_render_screen(client);
        return;

    } else {
        buffer_appendf(output, sizeof(output), &pos,
                       "Unknown command: %s\n"
                       "Type 'help' for available commands\n", cmd);
    }

    buffer_appendf(output, sizeof(output), &pos,
                   "\nPress any key to continue...");

    snprintf(client->command_output, sizeof(client->command_output), "%s", output);
    client->command_input[0] = '\0';
    tui_render_command_output(client);
}

/* Handle client key press - returns true if key was consumed */
static bool handle_key(client_t *client, unsigned char key, char *input) {
    /* Handle Ctrl+C (Exit or switch to NORMAL) */
    if (key == 3) {
        if (client->mode != MODE_NORMAL) {
            client->mode = MODE_NORMAL;
            client->command_input[0] = '\0';
            client->show_help = false;
            tui_render_screen(client);
        } else {
            /* In NORMAL mode, Ctrl+C exits */
            client->connected = false;
        }
        return true;
    }

    /* Handle help screen */
    if (client->show_help) {
        if (key == 'q' || key == 27) {
            client->show_help = false;
            tui_render_screen(client);
        } else if (key == 'e' || key == 'E') {
            client->help_lang = LANG_EN;
            client->help_scroll_pos = 0;
            tui_render_help(client);
        } else if (key == 'z' || key == 'Z') {
            client->help_lang = LANG_ZH;
            client->help_scroll_pos = 0;
            tui_render_help(client);
        } else if (key == 'j') {
            client->help_scroll_pos++;
            tui_render_help(client);
        } else if (key == 'k' && client->help_scroll_pos > 0) {
            client->help_scroll_pos--;
            tui_render_help(client);
        } else if (key == 'g') {
            client->help_scroll_pos = 0;
            tui_render_help(client);
        } else if (key == 'G') {
            client->help_scroll_pos = 999;  /* Large number */
            tui_render_help(client);
        }
        return true;  /* Key consumed */
    }

    /* Handle command output display */
    if (client->command_output[0] != '\0') {
        client->command_output[0] = '\0';
        client->mode = MODE_NORMAL;
        tui_render_screen(client);
        return true;  /* Key consumed */
    }

    /* Mode-specific handling */
    switch (client->mode) {
        case MODE_INSERT:
            if (key == 27) {  /* ESC */
                client->mode = MODE_NORMAL;
                client->scroll_pos = 0;
                tui_render_screen(client);
                return true;  /* Key consumed */
            } else if (key == '\r' || key == '\n') {  /* Enter */
                if (input[0] != '\0') {
                    message_t msg = {
                        .timestamp = time(NULL),
                    };
                    snprintf(msg.username, sizeof(msg.username), "%s", client->username);
                    snprintf(msg.content, sizeof(msg.content), "%s", input);
                    room_broadcast(g_room, &msg);
                    message_save(&msg);
                    input[0] = '\0';
                }
                tui_render_screen(client);
                return true;  /* Key consumed */
            } else if (key == 127 || key == 8) {  /* Backspace */
                if (input[0] != '\0') {
                    utf8_remove_last_char(input);
                    tui_render_input(client, input);
                }
                return true;  /* Key consumed */
            } else if (key == 23) { /* Ctrl+W (Delete Word) */
                if (input[0] != '\0') {
                    utf8_remove_last_word(input);
                    tui_render_input(client, input);
                }
                return true;
            } else if (key == 21) { /* Ctrl+U (Delete Line) */
                if (input[0] != '\0') {
                    input[0] = '\0';
                    tui_render_input(client, input);
                }
                return true;
            }
            break;

        case MODE_NORMAL:
            if (key == 'i') {
                client->mode = MODE_INSERT;
                tui_render_screen(client);
                return true;  /* Key consumed */
            } else if (key == ':') {
                client->mode = MODE_COMMAND;
                client->command_input[0] = '\0';
                tui_render_screen(client);
                return true;  /* Key consumed - prevents double colon */
            } else if (key == 'j') {
                /* Get message count atomically to prevent TOCTOU */
                int max_scroll = room_get_message_count(g_room);
                int msg_height = client->height - 3;
                if (msg_height < 1) msg_height = 1;
                max_scroll = max_scroll - msg_height;
                if (max_scroll < 0) max_scroll = 0;

                if (client->scroll_pos < max_scroll) {
                    client->scroll_pos++;
                    tui_render_screen(client);
                }
                return true;  /* Key consumed */
            } else if (key == 'k' && client->scroll_pos > 0) {
                client->scroll_pos--;
                tui_render_screen(client);
                return true;  /* Key consumed */
            } else if (key == 'g') {
                client->scroll_pos = 0;
                tui_render_screen(client);
                return true;  /* Key consumed */
            } else if (key == 'G') {
                /* Get message count atomically to prevent TOCTOU */
                int max_scroll = room_get_message_count(g_room);
                int msg_height = client->height - 3;
                if (msg_height < 1) msg_height = 1;
                max_scroll = max_scroll - msg_height;
                if (max_scroll < 0) max_scroll = 0;

                client->scroll_pos = max_scroll;
                tui_render_screen(client);
                return true;  /* Key consumed */
            } else if (key == '?') {
                client->show_help = true;
                client->help_scroll_pos = 0;
                tui_render_help(client);
                return true;  /* Key consumed */
            }
            break;

        case MODE_COMMAND:
            if (key == 27) {  /* ESC */
                client->mode = MODE_NORMAL;
                client->command_input[0] = '\0';
                tui_render_screen(client);
                return true;  /* Key consumed */
            } else if (key == '\r' || key == '\n') {
                execute_command(client);
                return true;  /* Key consumed */
            } else if (key == 127 || key == 8) {  /* Backspace */
                if (client->command_input[0] != '\0') {
                    utf8_remove_last_char(client->command_input);
                    tui_render_screen(client);
                }
                return true;  /* Key consumed */
            } else if (key == 23) { /* Ctrl+W (Delete Word) */
                if (client->command_input[0] != '\0') {
                    utf8_remove_last_word(client->command_input);
                    tui_render_screen(client);
                }
                return true;
            } else if (key == 21) { /* Ctrl+U (Delete Line) */
                if (client->command_input[0] != '\0') {
                    client->command_input[0] = '\0';
                    tui_render_screen(client);
                }
                return true;
            }
            break;

        default:
            break;
    }

    return false;  /* Key not consumed */
}

/* Handle client session */
void* client_handle_session(void *arg) {
    client_t *client = (client_t*)arg;
    char input[MAX_MESSAGE_LEN] = {0};
    char buf[4];
    bool joined_room = false;
    uint64_t seen_update_seq;
    time_t last_keepalive = time(NULL);

    /* Terminal size already set from PTY request */
    client->mode = MODE_INSERT;
    client->help_lang = LANG_ZH;
    client->connected = true;

    /* Check for exec command */
    if (client->exec_command[0] != '\0') {
        int exit_status = execute_exec_command(client);
        ssh_channel_request_send_exit_status(client->channel, exit_status);
        goto cleanup;
    }

    /* Read username */
    if (read_username(client) < 0) {
        goto cleanup;
    }

    /* Add to room */
    if (room_add_client(g_room, client) < 0) {
        client_printf(client, "Room is full\n");
        goto cleanup;
    }
    joined_room = true;

    /* Broadcast join message */
    message_t join_msg = {
        .timestamp = time(NULL),
    };
    strncpy(join_msg.username, "系统", MAX_USERNAME_LEN - 1);
    join_msg.username[MAX_USERNAME_LEN - 1] = '\0';
    snprintf(join_msg.content, MAX_MESSAGE_LEN, "%s 加入了聊天室", client->username);
    room_broadcast(g_room, &join_msg);

    /* Render initial screen */
    tui_render_screen(client);
    seen_update_seq = room_get_update_seq(g_room);

    /* Main input loop */
    while (client->connected && ssh_channel_is_open(client->channel)) {
        int ready = ssh_channel_poll_timeout(client->channel, 1000, 0);

        if (ready == SSH_ERROR) {
            break;
        }

        if (ready == 0) {
            bool room_updated = false;
            uint64_t current_update_seq = room_get_update_seq(g_room);

            if (!ssh_channel_is_open(client->channel)) {
                break;
            }

            if (current_update_seq != seen_update_seq) {
                seen_update_seq = current_update_seq;
                room_updated = true;
            }

            if (client->redraw_pending ||
                (room_updated && !client->show_help &&
                 client->command_output[0] == '\0')) {
                client->redraw_pending = false;

                if (client->show_help) {
                    tui_render_help(client);
                } else if (client->command_output[0] != '\0') {
                    tui_render_command_output(client);
                } else {
                    tui_render_screen(client);
                    if (client->mode == MODE_INSERT && input[0] != '\0') {
                        tui_render_input(client, input);
                    }
                }
            } else if (time(NULL) - last_keepalive >= 15) {
                if (ssh_send_keepalive(client->session) != SSH_OK) {
                    break;
                }
                last_keepalive = time(NULL);
            }
            continue;
        }

        int n = ssh_channel_read(client->channel, buf, 1, 0);

        if (n <= 0) {
            /* EOF or error */
            break;
        }

        last_keepalive = time(NULL);

        unsigned char b = buf[0];

        /* Handle special keys - returns true if key was consumed */
        bool key_consumed = handle_key(client, b, input);

        /* Only add character to input if not consumed by handle_key */
        if (!key_consumed) {
            /* Add character to input (INSERT mode only) */
            if (client->mode == MODE_INSERT && !client->show_help &&
                client->command_output[0] == '\0') {
                if (b >= 32 && b < 127) {  /* ASCII printable */
                    int len = strlen(input);
                    if (len < MAX_MESSAGE_LEN - 1) {
                        input[len] = b;
                        input[len + 1] = '\0';
                        tui_render_input(client, input);
                    }
                } else if (b >= 128) {  /* UTF-8 multi-byte */
                    int char_len = utf8_byte_length(b);
                    if (char_len <= 0 || char_len > 4) {
                        /* Invalid UTF-8 start byte */
                        continue;
                    }
                    buf[0] = b;
                    if (char_len > 1) {
                        int read_bytes = ssh_channel_read_timeout(client->channel, &buf[1], char_len - 1, 0, 5000);
                        if (read_bytes != char_len - 1) {
                            /* Incomplete or timed-out UTF-8 continuation */
                            continue;
                        }
                    }
                    /* Validate the complete UTF-8 sequence */
                    if (!utf8_is_valid_sequence(buf, char_len)) {
                        /* Invalid UTF-8 sequence */
                        continue;
                    }
                    int len = strlen(input);
                    if (len + char_len < MAX_MESSAGE_LEN - 1) {
                        memcpy(input + len, buf, char_len);
                        input[len + char_len] = '\0';
                        tui_render_input(client, input);
                    }
                }
            } else if (client->mode == MODE_COMMAND && !client->show_help &&
                       client->command_output[0] == '\0') {
                if (b >= 32 && b < 127) {  /* ASCII printable */
                    size_t len = strlen(client->command_input);
                    if (len < sizeof(client->command_input) - 1) {
                        client->command_input[len] = b;
                        client->command_input[len + 1] = '\0';
                        tui_render_screen(client);
                    }
                }
            }
        }
    }

cleanup:
    /* Broadcast leave message */
    if (joined_room) {
        message_t leave_msg = {
            .timestamp = time(NULL),
        };
        strncpy(leave_msg.username, "系统", MAX_USERNAME_LEN - 1);
        leave_msg.username[MAX_USERNAME_LEN - 1] = '\0';
        snprintf(leave_msg.content, MAX_MESSAGE_LEN, "%s 离开了聊天室", client->username);

        client->connected = false;
        room_remove_client(g_room, client);
        room_broadcast(g_room, &leave_msg);
    }

    release_ip_connection(client->client_ip);

    /* Release the callback reference (paired with addref before install_client_channel_callbacks) */
    client_release(client);

    /* Release the main reference - client will be freed when all refs are gone */
    client_release(client);

    /* Decrement connection count */
    decrement_connections();

    return NULL;
}

/* Authentication callbacks for callback-based API */

/* Password authentication callback */
static int auth_password(ssh_session session, const char *user,
                         const char *password, void *userdata) {
    session_context_t *ctx = (session_context_t *)userdata;

    if (user && user[0] != '\0') {
        strncpy(ctx->requested_user, user, sizeof(ctx->requested_user) - 1);
        ctx->requested_user[sizeof(ctx->requested_user) - 1] = '\0';
    }

    ctx->auth_attempts++;

    /* Limit auth attempts */
    if (ctx->auth_attempts > 3) {
        record_auth_failure(ctx->client_ip);
        fprintf(stderr, "Too many auth attempts from %s\n", ctx->client_ip);
        ssh_disconnect(session);
        return SSH_AUTH_DENIED;
    }

    /* If access token is configured, require it */
    if (g_access_token[0] != '\0') {
        if (password && constant_time_strcmp(password, g_access_token)) {
            /* Token matches */
            ctx->auth_success = true;
            return SSH_AUTH_SUCCESS;
        } else {
            /* Wrong token — IP blocking handles brute force, no sleep needed here
             * (sleeping in a libssh callback blocks the entire accept loop). */
            record_auth_failure(ctx->client_ip);
            return SSH_AUTH_DENIED;
        }
    } else {
        /* No token configured, accept any password */
        ctx->auth_success = true;
        return SSH_AUTH_SUCCESS;
    }
}

/* Passwordless (none) authentication callback */
static int auth_none(ssh_session session, const char *user, void *userdata) {
    (void)session;  /* Unused */
    session_context_t *ctx = (session_context_t *)userdata;

    if (user && user[0] != '\0') {
        strncpy(ctx->requested_user, user, sizeof(ctx->requested_user) - 1);
        ctx->requested_user[sizeof(ctx->requested_user) - 1] = '\0';
    }

    /* If access token is configured, reject passwordless */
    if (g_access_token[0] != '\0') {
        return SSH_AUTH_DENIED;
    } else {
        /* No token configured, allow passwordless */
        ctx->auth_success = true;
        return SSH_AUTH_SUCCESS;
    }
}

/* Public key authentication callback */
static int auth_pubkey(ssh_session session, const char *user,
                       struct ssh_key_struct *pubkey, char signature_state,
                       void *userdata) {
    (void)session;
    (void)pubkey;
    session_context_t *ctx = (session_context_t *)userdata;

    if (user && user[0] != '\0') {
        strncpy(ctx->requested_user, user, sizeof(ctx->requested_user) - 1);
        ctx->requested_user[sizeof(ctx->requested_user) - 1] = '\0';
    }

    /* Reject if access token is required (pubkey auth not supported with tokens) */
    if (g_access_token[0] != '\0') {
        return SSH_AUTH_DENIED;
    }

    /* Only accept after the signature has been verified by libssh.
     * SSH_PUBLICKEY_STATE_NONE is just a key offer — no proof of possession. */
    if (signature_state != SSH_PUBLICKEY_STATE_VALID) {
        return SSH_AUTH_PARTIAL;
    }

    ctx->auth_success = true;
    return SSH_AUTH_SUCCESS;
}

static void destroy_session_context(session_context_t *ctx) {
    if (!ctx) {
        return;
    }

    if (ctx->channel_cb) {
        free(ctx->channel_cb);
    }

    free(ctx);
}

static void cleanup_failed_session(ssh_session session, session_context_t *ctx) {
    if (ctx && ctx->channel) {
        if (ctx->channel_cb) {
            ssh_remove_channel_callbacks(ctx->channel, ctx->channel_cb);
        }
        ssh_channel_close(ctx->channel);
        ssh_channel_free(ctx->channel);
        ctx->channel = NULL;
    }

    if (session) {
        ssh_disconnect(session);
        ssh_free(session);
    }

    if (ctx) {
        release_ip_connection(ctx->client_ip);
    }
    destroy_session_context(ctx);
    decrement_connections();
}

static void setup_session_channel_callbacks(ssh_channel channel,
                                            session_context_t *ctx);
static int install_client_channel_callbacks(client_t *client);

/* Channel open callback */
static ssh_channel channel_open_request_session(ssh_session session, void *userdata) {
    session_context_t *ctx = (session_context_t *)userdata;
    ssh_channel channel;

    channel = ssh_channel_new(session);
    if (channel == NULL) {
        return NULL;
    }

    /* Store channel in context for main loop */
    ctx->channel = channel;

    /* Set up channel-specific callbacks (PTY, shell, exec) */
    setup_session_channel_callbacks(channel, ctx);

    return channel;
}

/* Channel callback functions */

/* PTY request callback */
static int channel_pty_request(ssh_session session, ssh_channel channel,
                               const char *term, int width, int height,
                               int pxwidth, int pxheight, void *userdata) {
    (void)session;   /* Unused */
    (void)channel;   /* Unused */
    (void)term;      /* Unused */
    (void)pxwidth;   /* Unused */
    (void)pxheight;  /* Unused */

    session_context_t *ctx = (session_context_t *)userdata;

    /* Store terminal dimensions */
    ctx->pty_width = width;
    ctx->pty_height = height;

    sanitize_terminal_size(&ctx->pty_width, &ctx->pty_height);

    return SSH_OK;
}

static int channel_pty_window_change(ssh_session session, ssh_channel channel,
                                     int width, int height,
                                     int pxwidth, int pxheight,
                                     void *userdata) {
    (void)session;
    (void)channel;
    (void)pxwidth;
    (void)pxheight;

    session_context_t *ctx = (session_context_t *)userdata;

    ctx->pty_width = width;
    ctx->pty_height = height;
    sanitize_terminal_size(&ctx->pty_width, &ctx->pty_height);

    return SSH_OK;
}

/* Shell request callback */
static int channel_shell_request(ssh_session session, ssh_channel channel,
                                 void *userdata) {
    (void)session;  /* Unused */
    (void)channel;  /* Unused */

    session_context_t *ctx = (session_context_t *)userdata;

    /* Mark channel as ready */
    ctx->channel_ready = true;

    /* Accept shell request */
    return SSH_OK;
}

/* Exec request callback */
static int channel_exec_request(ssh_session session, ssh_channel channel,
                                const char *command, void *userdata) {
    (void)session;  /* Unused */
    (void)channel;  /* Unused */

    session_context_t *ctx = (session_context_t *)userdata;

    /* Store exec command */
    if (command) {
        strncpy(ctx->exec_command, command, sizeof(ctx->exec_command) - 1);
        ctx->exec_command[sizeof(ctx->exec_command) - 1] = '\0';
    }

    /* Mark channel as ready */
    ctx->channel_ready = true;

    return SSH_OK;
}

/* Set up channel callbacks */
static void setup_session_channel_callbacks(ssh_channel channel,
                                            session_context_t *ctx) {
    /* Allocate channel callbacks on heap to persist */
    ctx->channel_cb = calloc(1, sizeof(struct ssh_channel_callbacks_struct));
    if (!ctx->channel_cb) {
        return;
    }

    ssh_callbacks_init(ctx->channel_cb);

    ctx->channel_cb->userdata = ctx;
    ctx->channel_cb->channel_pty_request_function = channel_pty_request;
    ctx->channel_cb->channel_shell_request_function = channel_shell_request;
    ctx->channel_cb->channel_pty_window_change_function = channel_pty_window_change;
    ctx->channel_cb->channel_exec_request_function = channel_exec_request;

    ssh_set_channel_callbacks(channel, ctx->channel_cb);
}

static int client_channel_window_change(ssh_session session, ssh_channel channel,
                                        int width, int height,
                                        int pxwidth, int pxheight,
                                        void *userdata) {
    (void)session;
    (void)channel;
    (void)pxwidth;
    (void)pxheight;

    client_t *client = (client_t *)userdata;
    if (!client) {
        return SSH_ERROR;
    }

    pthread_mutex_lock(&client->io_lock);
    client->width = width;
    client->height = height;
    sanitize_terminal_size(&client->width, &client->height);
    pthread_mutex_unlock(&client->io_lock);
    client->redraw_pending = true;
    return SSH_OK;
}

static void client_channel_eof(ssh_session session, ssh_channel channel,
                               void *userdata) {
    (void)session;
    (void)channel;

    client_t *client = (client_t *)userdata;
    if (client) {
        client->connected = false;
    }
}

static void client_channel_close(ssh_session session, ssh_channel channel,
                                 void *userdata) {
    (void)session;
    (void)channel;

    client_t *client = (client_t *)userdata;
    if (client) {
        client->connected = false;
    }
}

static int install_client_channel_callbacks(client_t *client) {
    if (!client || !client->channel) {
        return -1;
    }

    client->channel_cb = calloc(1, sizeof(struct ssh_channel_callbacks_struct));
    if (!client->channel_cb) {
        return -1;
    }

    ssh_callbacks_init(client->channel_cb);
    client->channel_cb->userdata = client;
    client->channel_cb->channel_eof_function = client_channel_eof;
    client->channel_cb->channel_close_function = client_channel_close;
    client->channel_cb->channel_pty_window_change_function =
        client_channel_window_change;

    if (ssh_set_channel_callbacks(client->channel, client->channel_cb) != SSH_OK) {
        free(client->channel_cb);
        client->channel_cb = NULL;
        return -1;
    }

    return 0;
}

static void *bootstrap_client_session(void *arg) {
    accepted_session_t *accepted = (accepted_session_t *)arg;
    ssh_session session;
    session_context_t *ctx = NULL;
    ssh_event event = NULL;
    struct ssh_server_callbacks_struct server_cb;
    ssh_channel channel;
    client_t *client = NULL;
    bool timed_out = false;
    time_t start_time;
    char accepted_ip[INET6_ADDRSTRLEN] = "";

    if (!accepted) {
        return NULL;
    }

    session = accepted->session;
    if (accepted->client_ip[0] != '\0') {
        snprintf(accepted_ip, sizeof(accepted_ip), "%s", accepted->client_ip);
    }
    free(accepted);

    ctx = calloc(1, sizeof(session_context_t));
    if (!ctx) {
        release_ip_connection(accepted_ip);
        ssh_disconnect(session);
        ssh_free(session);
        decrement_connections();
        return NULL;
    }

    if (accepted_ip[0] != '\0') {
        snprintf(ctx->client_ip, sizeof(ctx->client_ip), "%s", accepted_ip);
    } else {
        get_client_ip(session, ctx->client_ip, sizeof(ctx->client_ip));
    }
    ctx->pty_width = 80;
    ctx->pty_height = 24;
    ctx->exec_command[0] = '\0';
    ctx->requested_user[0] = '\0';
    ctx->auth_success = false;
    ctx->auth_attempts = 0;
    ctx->channel_ready = false;
    ctx->channel = NULL;
    ctx->channel_cb = NULL;

    memset(&server_cb, 0, sizeof(server_cb));
    ssh_callbacks_init(&server_cb);
    server_cb.userdata = ctx;
    server_cb.auth_password_function = auth_password;
    server_cb.auth_none_function = auth_none;
    server_cb.auth_pubkey_function = auth_pubkey;
    server_cb.channel_open_request_session_function = channel_open_request_session;
    ssh_set_server_callbacks(session, &server_cb);

    if (ssh_handle_key_exchange(session) != SSH_OK) {
        fprintf(stderr, "Key exchange failed from %s: %s\n",
                ctx->client_ip, ssh_get_error(session));
        cleanup_failed_session(session, ctx);
        return NULL;
    }

    event = ssh_event_new();
    if (!event) {
        fprintf(stderr, "Failed to create SSH event for %s\n", ctx->client_ip);
        cleanup_failed_session(session, ctx);
        return NULL;
    }

    if (ssh_event_add_session(event, session) != SSH_OK) {
        fprintf(stderr, "Failed to add session to event loop for %s\n",
                ctx->client_ip);
        ssh_event_free(event);
        cleanup_failed_session(session, ctx);
        return NULL;
    }

    start_time = time(NULL);
    while ((!ctx->auth_success || ctx->channel == NULL || !ctx->channel_ready) &&
           !timed_out) {
        int rc = ssh_event_dopoll(event, 1000);

        if (rc == SSH_ERROR) {
            fprintf(stderr, "Event poll error from %s: %s\n",
                    ctx->client_ip, ssh_get_error(session));
            break;
        }

        if (time(NULL) - start_time > 30) {
            timed_out = true;
        }
    }

    ssh_event_free(event);
    event = NULL;

    if (!ctx->auth_success) {
        fprintf(stderr, "Authentication failed or timed out from %s\n",
                ctx->client_ip);
        cleanup_failed_session(session, ctx);
        return NULL;
    }

    channel = ctx->channel;
    if (!channel || !ctx->channel_ready || timed_out) {
        fprintf(stderr, "Failed to open/setup channel from %s\n",
                ctx->client_ip);
        cleanup_failed_session(session, ctx);
        return NULL;
    }

    client = calloc(1, sizeof(client_t));
    if (!client) {
        cleanup_failed_session(session, ctx);
        return NULL;
    }

    client->session = session;
    client->channel = channel;
    client->width = ctx->pty_width;
    client->height = ctx->pty_height;
    sanitize_terminal_size(&client->width, &client->height);
    client->ref_count = 1;
    pthread_mutex_init(&client->ref_lock, NULL);
    pthread_mutex_init(&client->io_lock, NULL);

    if (ctx->requested_user[0] != '\0') {
        strncpy(client->ssh_login, ctx->requested_user,
                sizeof(client->ssh_login) - 1);
        client->ssh_login[sizeof(client->ssh_login) - 1] = '\0';
    }
    if (ctx->client_ip[0] != '\0') {
        snprintf(client->client_ip, sizeof(client->client_ip), "%s",
                 ctx->client_ip);
    }
    if (ctx->exec_command[0] != '\0') {
        strncpy(client->exec_command, ctx->exec_command,
                sizeof(client->exec_command) - 1);
        client->exec_command[sizeof(client->exec_command) - 1] = '\0';
    }

    /* Add a ref for the channel callbacks (eof/close/window_change) so the
     * client_t outlives any in-flight callback invocation. */
    client_addref(client);

    if (install_client_channel_callbacks(client) < 0) {
        /* Nullify session/channel ownership so client_release won't
         * double-free what cleanup_failed_session is about to free. */
        client->session = NULL;
        client->channel = NULL;
        client_release(client);  /* drop the callback ref (2 → 1) */
        client_release(client);  /* drop the main ref (1 → 0, frees client) */
        cleanup_failed_session(session, ctx);
        return NULL;
    }

    if (ctx->channel_cb) {
        ssh_remove_channel_callbacks(channel, ctx->channel_cb);
        free(ctx->channel_cb);
        ctx->channel_cb = NULL;
    }
    destroy_session_context(ctx);

    client_handle_session(client);
    return NULL;
}

/* Initialize SSH server */
int ssh_server_init(int port) {
    /* Initialize rate limiting configuration */
    init_rate_limit_config();
    g_listen_port = port;
    g_server_start_time = time(NULL);

    g_sshbind = ssh_bind_new();
    if (!g_sshbind) {
        fprintf(stderr, "Failed to create SSH bind\n");
        return -1;
    }

    /* Set up host key */
    if (setup_host_key(g_sshbind) < 0) {
        ssh_bind_free(g_sshbind);
        return -1;
    }

    /* Bind to port */
    ssh_bind_options_set(g_sshbind, SSH_BIND_OPTIONS_BINDPORT, &port);

    /* Configurable bind address (default: 0.0.0.0) */
    const char *bind_addr = getenv("TNT_BIND_ADDR");
    if (!bind_addr) {
        bind_addr = "0.0.0.0";
    }
    ssh_bind_options_set(g_sshbind, SSH_BIND_OPTIONS_BINDADDR, bind_addr);

    /* Configurable SSH log level (default: SSH_LOG_WARNING=1) */
    int verbosity = env_int("TNT_SSH_LOG_LEVEL", SSH_LOG_WARNING, 0, 4);
    ssh_bind_options_set(g_sshbind, SSH_BIND_OPTIONS_LOG_VERBOSITY, &verbosity);

    if (ssh_bind_listen(g_sshbind) < 0) {
        fprintf(stderr, "Failed to bind to port %d: %s\n", port, ssh_get_error(g_sshbind));
        ssh_bind_free(g_sshbind);
        return -1;
    }

    return 0;
}

/* Start SSH server (blocking) */
int ssh_server_start(int unused) {
    (void)unused;
    const char *public_host = getenv("TNT_PUBLIC_HOST");
    pthread_attr_t attr;
    if (!public_host || public_host[0] == '\0') {
        public_host = "localhost";
    }

    printf("TNT chat server listening on port %d (SSH)\n", g_listen_port);
    printf("Connect with: ssh -p %d %s\n", g_listen_port, public_host);
    fflush(stdout);

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    while (1) {
        ssh_session session = ssh_new();
        char client_ip[INET6_ADDRSTRLEN];
        accepted_session_t *accepted;
        pthread_t thread;

        if (!session) {
            fprintf(stderr, "Failed to create SSH session\n");
            continue;
        }

        /* Accept connection */
        if (ssh_bind_accept(g_sshbind, session) != SSH_OK) {
            fprintf(stderr, "Error accepting connection: %s\n", ssh_get_error(g_sshbind));
            ssh_free(session);
            continue;
        }

        get_client_ip(session, client_ip, sizeof(client_ip));

        /* Check total connection limit */
        if (!check_and_increment_connections()) {
            fprintf(stderr, "Max connections reached, rejecting %s\n", client_ip);
            ssh_disconnect(session);
            ssh_free(session);
            continue;
        }

        if (!check_ip_connection_policy(client_ip)) {
            decrement_connections();
            ssh_disconnect(session);
            ssh_free(session);
            continue;
        }

        accepted = calloc(1, sizeof(*accepted));
        if (!accepted) {
            release_ip_connection(client_ip);
            decrement_connections();
            ssh_disconnect(session);
            ssh_free(session);
            continue;
        }

        accepted->session = session;
        snprintf(accepted->client_ip, sizeof(accepted->client_ip), "%s",
                 client_ip);

        if (pthread_create(&thread, &attr, bootstrap_client_session, accepted) != 0) {
            fprintf(stderr, "Thread creation failed: %s\n", strerror(errno));
            free(accepted);
            release_ip_connection(client_ip);
            decrement_connections();
            ssh_disconnect(session);
            ssh_free(session);
            continue;
        }
    }
    /* Unreachable — the while(1) loop only exits via signal/_exit(). */
}
