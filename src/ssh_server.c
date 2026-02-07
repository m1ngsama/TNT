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
#include <stdarg.h>
#include <sys/stat.h>

/* Global SSH bind instance */
static ssh_bind g_sshbind = NULL;

/* Session context for callback-based API */
typedef struct {
    char client_ip[INET6_ADDRSTRLEN];
    int pty_width;
    int pty_height;
    char exec_command[256];
    bool auth_success;
    int auth_attempts;
    bool channel_ready;  /* Set when shell/exec request received */
    ssh_channel channel;  /* Channel created in callback */
    struct ssh_channel_callbacks_struct *channel_cb;  /* Channel callbacks */
} session_context_t;

/* Rate limiting and connection tracking */
#define MAX_TRACKED_IPS 256
#define RATE_LIMIT_WINDOW 60        /* seconds */
#define MAX_CONN_PER_WINDOW 10      /* connections per IP per window */
#define MAX_AUTH_FAILURES 5         /* auth failures before block */
#define BLOCK_DURATION 300          /* seconds to block after too many failures */

typedef struct {
    char ip[INET6_ADDRSTRLEN];
    time_t window_start;
    int connection_count;
    int auth_failure_count;
    bool is_blocked;
    time_t block_until;
} ip_rate_limit_t;

static ip_rate_limit_t g_rate_limits[MAX_TRACKED_IPS];
static pthread_mutex_t g_rate_limit_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_total_connections = 0;
static pthread_mutex_t g_conn_count_lock = PTHREAD_MUTEX_INITIALIZER;

/* Configuration from environment variables */
static int g_max_connections = 64;
static int g_max_conn_per_ip = 5;
static int g_rate_limit_enabled = 1;
static char g_access_token[256] = "";

/* Initialize rate limit configuration from environment */
static void init_rate_limit_config(void) {
    const char *env;

    if ((env = getenv("TNT_MAX_CONNECTIONS")) != NULL) {
        int val = atoi(env);
        if (val > 0 && val <= 1024) {
            g_max_connections = val;
        }
    }

    if ((env = getenv("TNT_MAX_CONN_PER_IP")) != NULL) {
        int val = atoi(env);
        if (val > 0 && val <= 100) {
            g_max_conn_per_ip = val;
        }
    }

    if ((env = getenv("TNT_RATE_LIMIT")) != NULL) {
        g_rate_limit_enabled = atoi(env);
    }

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
            g_rate_limits[i].connection_count = 0;
            g_rate_limits[i].auth_failure_count = 0;
            g_rate_limits[i].is_blocked = false;
            g_rate_limits[i].block_until = 0;
            return &g_rate_limits[i];
        }
    }

    /* Find oldest entry to replace */
    int oldest_idx = 0;
    time_t oldest_time = g_rate_limits[0].window_start;
    for (int i = 1; i < MAX_TRACKED_IPS; i++) {
        if (g_rate_limits[i].window_start < oldest_time) {
            oldest_time = g_rate_limits[i].window_start;
            oldest_idx = i;
        }
    }

    /* Reset and reuse */
    strncpy(g_rate_limits[oldest_idx].ip, ip, sizeof(g_rate_limits[oldest_idx].ip) - 1);
    g_rate_limits[oldest_idx].ip[sizeof(g_rate_limits[oldest_idx].ip) - 1] = '\0';
    g_rate_limits[oldest_idx].window_start = time(NULL);
    g_rate_limits[oldest_idx].connection_count = 0;
    g_rate_limits[oldest_idx].auth_failure_count = 0;
    g_rate_limits[oldest_idx].is_blocked = false;
    g_rate_limits[oldest_idx].block_until = 0;
    return &g_rate_limits[oldest_idx];
}

/* Check rate limit for an IP */
static bool check_rate_limit(const char *ip) {
    if (!g_rate_limit_enabled) {
        return true;
    }

    time_t now = time(NULL);

    pthread_mutex_lock(&g_rate_limit_lock);
    ip_rate_limit_t *entry = get_rate_limit_entry(ip);

    /* Check if blocked */
    if (entry->is_blocked && now < entry->block_until) {
        pthread_mutex_unlock(&g_rate_limit_lock);
        fprintf(stderr, "Blocked IP %s (blocked until %ld)\n", ip, (long)entry->block_until);
        return false;
    }

    /* Unblock if block duration passed */
    if (entry->is_blocked && now >= entry->block_until) {
        entry->is_blocked = false;
        entry->auth_failure_count = 0;
    }

    /* Reset window if expired */
    if (now - entry->window_start >= RATE_LIMIT_WINDOW) {
        entry->window_start = now;
        entry->connection_count = 0;
    }

    /* Check connection rate */
    entry->connection_count++;
    if (entry->connection_count > MAX_CONN_PER_WINDOW) {
        entry->is_blocked = true;
        entry->block_until = now + BLOCK_DURATION;
        pthread_mutex_unlock(&g_rate_limit_lock);
        fprintf(stderr, "Rate limit exceeded for IP %s\n", ip);
        return false;
    }

    pthread_mutex_unlock(&g_rate_limit_lock);
    return true;
}

/* Record authentication failure */
static void record_auth_failure(const char *ip) {
    time_t now = time(NULL);

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

    /* Check if host key exists */
    if (stat(HOST_KEY_FILE, &st) == 0) {
        /* Validate file size */
        if (st.st_size == 0) {
            fprintf(stderr, "Warning: Empty key file, regenerating...\n");
            unlink(HOST_KEY_FILE);
            /* Fall through to generate new key */
        } else if (st.st_size > 10 * 1024 * 1024) {
            /* Sanity check: key file shouldn't be > 10MB */
            fprintf(stderr, "Error: Key file too large (%lld bytes)\n", (long long)st.st_size);
            return -1;
        } else {
            /* Verify and fix permissions */
            if ((st.st_mode & 0077) != 0) {
                fprintf(stderr, "Warning: Fixing insecure key file permissions\n");
                chmod(HOST_KEY_FILE, 0600);
            }

            /* Load existing key */
            if (ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_RSAKEY, HOST_KEY_FILE) < 0) {
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
    char temp_key_file[256];
    snprintf(temp_key_file, sizeof(temp_key_file), "%s.tmp.%d", HOST_KEY_FILE, getpid());

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
    if (rename(temp_key_file, HOST_KEY_FILE) < 0) {
        fprintf(stderr, "Failed to rename temporary key file\n");
        unlink(temp_key_file);
        return -1;
    }

    /* Load the newly created key */
    if (ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_RSAKEY, HOST_KEY_FILE) < 0) {
        fprintf(stderr, "Failed to load host key: %s\n", ssh_get_error(sshbind));
        return -1;
    }

    return 0;
}

/* Send data to client via SSH channel */
int client_send(client_t *client, const char *data, size_t len) {
    if (!client || !client->connected || !client->channel) return -1;

    int sent = ssh_channel_write(client->channel, data, len);
    return (sent < 0) ? -1 : 0;
}

/* Increment client reference count - currently unused but kept for future use */
static void client_addref(client_t *client) __attribute__((unused));
static void client_addref(client_t *client) {
    if (!client) return;
    pthread_mutex_lock(&client->ref_lock);
    client->ref_count++;
    pthread_mutex_unlock(&client->ref_lock);
}

/* Decrement client reference count and free if zero */
static void client_release(client_t *client) {
    if (!client) return;

    pthread_mutex_lock(&client->ref_lock);
    client->ref_count--;
    int count = client->ref_count;
    pthread_mutex_unlock(&client->ref_lock);

    if (count == 0) {
        /* Safe to free now */
        if (client->channel) {
            ssh_channel_close(client->channel);
            ssh_channel_free(client->channel);
        }
        if (client->session) {
            ssh_disconnect(client->session);
            ssh_free(client->session);
        }
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
                int read_bytes = ssh_channel_read(client->channel, &buf[1], len - 1, 0);
                if (read_bytes != len - 1) {
                    /* Incomplete UTF-8 */
                    continue;
                }
            }
            /* Validate the complete UTF-8 sequence */
            if (!utf8_is_valid_sequence(buf, len)) {
                /* Invalid UTF-8 sequence */
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
            sleep(1);  /* Slow down rapid retry attempts */
        } else {
            /* Truncate to 20 characters */
            if (utf8_strlen(client->username) > 20) {
                utf8_truncate(client->username, 20);
            }
        }
    }

    return 0;
}

/* Execute a command */
static void execute_command(client_t *client) {
    char *cmd = client->command_input;
    char output[2048] = {0};
    int pos = 0;

    /* Trim whitespace */
    while (*cmd == ' ') cmd++;
    char *end = cmd + strlen(cmd) - 1;
    while (end > cmd && *end == ' ') {
        *end = '\0';
        end--;
    }

    if (strcmp(cmd, "list") == 0 || strcmp(cmd, "users") == 0 ||
        strcmp(cmd, "who") == 0) {
        pos += snprintf(output + pos, sizeof(output) - pos,
                       "========================================\n"
                       "     Online Users / 在线用户\n"
                       "========================================\n");

        pthread_rwlock_rdlock(&g_room->lock);
        pos += snprintf(output + pos, sizeof(output) - pos,
                       "Total / 总数: %d\n"
                       "----------------------------------------\n",
                       g_room->client_count);

        for (int i = 0; i < g_room->client_count; i++) {
            char marker = (g_room->clients[i] == client) ? '*' : ' ';
            pos += snprintf(output + pos, sizeof(output) - pos,
                           "%c %d. %s\n", marker, i + 1,
                           g_room->clients[i]->username);
        }

        pthread_rwlock_unlock(&g_room->lock);

        pos += snprintf(output + pos, sizeof(output) - pos,
                       "========================================\n"
                       "* = you / 你\n");

    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "commands") == 0) {
        pos += snprintf(output + pos, sizeof(output) - pos,
                       "========================================\n"
                       "        Available Commands\n"
                       "========================================\n"
                       "list, users, who - Show online users\n"
                       "help, commands   - Show this help\n"
                       "clear, cls       - Clear command output\n"
                       "========================================\n");

    } else if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0) {
        pos += snprintf(output + pos, sizeof(output) - pos,
                       "Command output cleared\n");

    } else if (cmd[0] == '\0') {
        /* Empty command */
        client->mode = MODE_NORMAL;
        client->command_input[0] = '\0';
        tui_render_screen(client);
        return;

    } else {
        pos += snprintf(output + pos, sizeof(output) - pos,
                       "Unknown command: %s\n"
                       "Type 'help' for available commands\n", cmd);
    }

    pos += snprintf(output + pos, sizeof(output) - pos,
                   "\nPress any key to continue...");

    strncpy(client->command_output, output, sizeof(client->command_output) - 1);
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
                    strncpy(msg.username, client->username, MAX_USERNAME_LEN - 1);
                    strncpy(msg.content, input, MAX_MESSAGE_LEN - 1);
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

    /* Terminal size already set from PTY request */
    client->mode = MODE_INSERT;
    client->help_lang = LANG_ZH;
    client->connected = true;

    /* Check for exec command */
    if (client->exec_command[0] != '\0') {
        if (strcmp(client->exec_command, "exit") == 0) {
            /* Just exit */
            ssh_channel_request_send_exit_status(client->channel, 0);
            goto cleanup;
        } else {
            /* Unknown command */
            client_printf(client, "Command not supported: %s\r\nOnly 'exit' is supported in non-interactive mode.\r\n", client->exec_command);
            ssh_channel_request_send_exit_status(client->channel, 1);
            goto cleanup;
        }
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

    /* Main input loop */
    while (client->connected && ssh_channel_is_open(client->channel)) {
        /* Use non-blocking read with timeout */
        int n = ssh_channel_read_timeout(client->channel, buf, 1, 0, 30000); /* 30 sec timeout */

        if (n == SSH_AGAIN) {
            /* Timeout - check if channel is still alive */
            if (!ssh_channel_is_open(client->channel)) {
                break;
            }
            continue;
        }

        if (n == SSH_ERROR) {
            /* Read error - connection likely closed */
            break;
        }

        if (n <= 0) {
            /* EOF or error */
            break;
        }

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
                        int read_bytes = ssh_channel_read(client->channel, &buf[1], char_len - 1, 0);
                        if (read_bytes != char_len - 1) {
                            /* Incomplete UTF-8 sequence */
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
    {
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
        if (password && strcmp(password, g_access_token) == 0) {
            /* Token matches */
            ctx->auth_success = true;
            return SSH_AUTH_SUCCESS;
        } else {
            /* Wrong token */
            record_auth_failure(ctx->client_ip);
            sleep(2);  /* Slow down brute force */
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
    (void)user;     /* Unused */
    session_context_t *ctx = (session_context_t *)userdata;

    /* If access token is configured, reject passwordless */
    if (g_access_token[0] != '\0') {
        return SSH_AUTH_DENIED;
    } else {
        /* No token configured, allow passwordless */
        ctx->auth_success = true;
        return SSH_AUTH_SUCCESS;
    }
}

/* Forward declaration of channel callbacks setup */
static void setup_channel_callbacks(ssh_channel channel, session_context_t *ctx);

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
    setup_channel_callbacks(channel, ctx);

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

    /* Default to 80x24 if invalid */
    if (ctx->pty_width <= 0 || ctx->pty_width > 500) ctx->pty_width = 80;
    if (ctx->pty_height <= 0 || ctx->pty_height > 200) ctx->pty_height = 24;

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
static void setup_channel_callbacks(ssh_channel channel, session_context_t *ctx) {
    /* Allocate channel callbacks on heap to persist */
    ctx->channel_cb = calloc(1, sizeof(struct ssh_channel_callbacks_struct));
    if (!ctx->channel_cb) {
        return;
    }

    ssh_callbacks_init(ctx->channel_cb);

    ctx->channel_cb->userdata = ctx;
    ctx->channel_cb->channel_pty_request_function = channel_pty_request;
    ctx->channel_cb->channel_shell_request_function = channel_shell_request;
    ctx->channel_cb->channel_exec_request_function = channel_exec_request;

    ssh_set_channel_callbacks(channel, ctx->channel_cb);
}

/* Initialize SSH server */
int ssh_server_init(int port) {
    /* Initialize rate limiting configuration */
    init_rate_limit_config();

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
    int verbosity = SSH_LOG_WARNING;
    const char *log_level_env = getenv("TNT_SSH_LOG_LEVEL");
    if (log_level_env) {
        int level = atoi(log_level_env);
        if (level >= 0 && level <= 4) {
            verbosity = level;
        }
    }
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

    printf("TNT chat server listening on port %d (SSH)\n", DEFAULT_PORT);
    printf("Connect with: ssh -p %d localhost\n", DEFAULT_PORT);

    while (1) {
        ssh_session session = ssh_new();
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

        /* Create session context for callbacks */
        session_context_t *ctx = calloc(1, sizeof(session_context_t));
        if (!ctx) {
            ssh_disconnect(session);
            ssh_free(session);
            continue;
        }

        /* Initialize context */
        get_client_ip(session, ctx->client_ip, sizeof(ctx->client_ip));
        ctx->pty_width = 80;   /* Default */
        ctx->pty_height = 24;  /* Default */
        ctx->exec_command[0] = '\0';
        ctx->auth_success = false;
        ctx->auth_attempts = 0;
        ctx->channel_ready = false;
        ctx->channel = NULL;
        ctx->channel_cb = NULL;

        /* Check rate limit */
        if (!check_rate_limit(ctx->client_ip)) {
            ssh_disconnect(session);
            ssh_free(session);
            free(ctx);
            sleep(1);  /* Slow down blocked clients */
            continue;
        }

        /* Check total connection limit */
        if (!check_and_increment_connections()) {
            fprintf(stderr, "Max connections reached, rejecting %s\n", ctx->client_ip);
            ssh_disconnect(session);
            ssh_free(session);
            free(ctx);
            sleep(1);
            continue;
        }

        /* Set up server callbacks (auth and channel) */
        struct ssh_server_callbacks_struct server_cb;
        memset(&server_cb, 0, sizeof(server_cb));
        ssh_callbacks_init(&server_cb);

        server_cb.userdata = ctx;
        server_cb.auth_password_function = auth_password;
        server_cb.auth_none_function = auth_none;
        server_cb.channel_open_request_session_function = channel_open_request_session;

        ssh_set_server_callbacks(session, &server_cb);

        /* Perform key exchange */
        if (ssh_handle_key_exchange(session) != SSH_OK) {
            fprintf(stderr, "Key exchange failed: %s\n", ssh_get_error(session));
            decrement_connections();
            ssh_disconnect(session);
            ssh_free(session);
            free(ctx);
            sleep(1);
            continue;
        }

        /* Event loop to handle authentication and channel setup */
        ssh_event event = ssh_event_new();
        if (event == NULL) {
            fprintf(stderr, "Failed to create event\n");
            decrement_connections();
            ssh_disconnect(session);
            ssh_free(session);
            free(ctx);
            continue;
        }

        ssh_event_add_session(event, session);

        /* Wait for: auth success, channel open, AND channel ready (PTY/shell/exec) */
        int timeout_sec = 30;
        time_t start_time = time(NULL);
        bool timed_out = false;
        ssh_channel channel = NULL;

        while ((!ctx->auth_success || ctx->channel == NULL || !ctx->channel_ready) && !timed_out) {
            /* Poll with 1 second timeout per iteration */
            int rc = ssh_event_dopoll(event, 1000);

            if (rc == SSH_ERROR) {
                fprintf(stderr, "Event poll error: %s\n", ssh_get_error(session));
                break;
            }

            /* Check timeout */
            if (time(NULL) - start_time > timeout_sec) {
                timed_out = true;
            }
        }

        ssh_event_free(event);

        /* Check if authentication succeeded */
        if (!ctx->auth_success) {
            fprintf(stderr, "Authentication failed or timed out from %s\n", ctx->client_ip);
            decrement_connections();
            ssh_disconnect(session);
            ssh_free(session);
            if (ctx->channel_cb) free(ctx->channel_cb);
            free(ctx);
            sleep(2);  /* Longer delay for auth failures */
            continue;
        }

        /* Check if channel opened and is ready */
        channel = ctx->channel;
        if (!channel || !ctx->channel_ready || timed_out) {
            fprintf(stderr, "Failed to open/setup channel from %s\n", ctx->client_ip);
            decrement_connections();
            ssh_disconnect(session);
            ssh_free(session);
            if (ctx->channel_cb) free(ctx->channel_cb);
            free(ctx);
            continue;
        }

        /* Create client structure */
        client_t *client = calloc(1, sizeof(client_t));
        if (!client) {
            ssh_channel_close(channel);
            ssh_channel_free(channel);
            ssh_disconnect(session);
            ssh_free(session);
            free(ctx);
            continue;
        }

        /* Initialize client from context */
        client->session = session;
        client->channel = channel;
        client->fd = -1;  /* Not used with SSH */
        client->width = ctx->pty_width;
        client->height = ctx->pty_height;
        client->ref_count = 1;  /* Initial reference */
        pthread_mutex_init(&client->ref_lock, NULL);

        /* Copy exec command if any */
        if (ctx->exec_command[0] != '\0') {
            strncpy(client->exec_command, ctx->exec_command, sizeof(client->exec_command) - 1);
            client->exec_command[sizeof(client->exec_command) - 1] = '\0';
        }

        /* Free context and channel callbacks - no longer needed */
        if (ctx->channel_cb) free(ctx->channel_cb);
        free(ctx);

        /* Create thread for client */
        pthread_t thread;
        pthread_attr_t attr;

        /* Initialize thread attributes for detached thread */
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        if (pthread_create(&thread, &attr, client_handle_session, client) != 0) {
            fprintf(stderr, "Thread creation failed: %s\n", strerror(errno));
            pthread_attr_destroy(&attr);
            /* Clean up all resources */
            pthread_mutex_destroy(&client->ref_lock);
            ssh_channel_close(channel);
            ssh_channel_free(channel);
            ssh_disconnect(session);
            ssh_free(session);
            free(client);
            continue;
        }

        pthread_attr_destroy(&attr);
    }

    return 0;
}
