#include "ssh_server.h"
#include "commands.h"
#include "exec.h"
#include "ratelimit.h"
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

static time_t g_server_start_time = 0;

time_t ssh_server_start_time(void) {
    return g_server_start_time;
}

/* Configuration from environment variables.  Rate-limiting / connection-count
 * config has moved to ratelimit.{c,h}; the two below stay here until the auth
 * and input modules are extracted in later PR2 steps. */
static char g_access_token[256] = "";
static int g_idle_timeout = DEFAULT_IDLE_TIMEOUT;

/* Constant-time string comparison to prevent timing side-channel attacks.
 * Always iterates over the full length of the secret (b) to avoid leaking
 * its length.  When the input (a) is shorter, compares against zero bytes;
 * the length mismatch is folded into the result separately.
 *
 * Note: the length-diff is accumulated in size_t to avoid the bug where a
 * narrower type (e.g. unsigned char) would collapse pairs like (300, 44) to
 * 0 because 300 ^ 44 == 256 ^ (44 ^ 44) == 256 which truncates to 0. */
static bool constant_time_strcmp(const char *a, const char *b) {
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    volatile size_t length_diff = len_a ^ len_b;
    volatile unsigned char byte_diff = 0;
    for (size_t i = 0; i < len_b; i++) {
        unsigned char ca = (i < len_a) ? (unsigned char)a[i] : 0;
        byte_diff |= ca ^ (unsigned char)b[i];
    }
    return length_diff == 0 && byte_diff == 0;
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
                /* Compute width of the last character before removing it */
                int old_pos = pos;
                int ci = pos - 1;
                while (ci > 0 && (username[ci] & 0xC0) == 0x80) ci--;
                int bytes_read;
                uint32_t cp = utf8_decode(username + ci, &bytes_read);
                int w = utf8_char_width(cp);
                utf8_remove_last_char(username);
                pos = strlen(username);
                (void)old_pos;
                for (int j = 0; j < w; j++)
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

/* Notify any clients whose usernames appear as @mentions in `content`.
 * Lives here because it bridges chat_room (target lookup) and the client
 * I/O API; will move into a proper home when a `client.c` is carved out
 * during PR2-M6 server cleanup. */
void notify_mentions(const char *content, const client_t *sender) {
    pthread_rwlock_rdlock(&g_room->lock);
    int count = g_room->client_count;
    client_t *targets[MAX_CLIENTS];
    int target_count = 0;

    for (int i = 0; i < count; i++) {
        client_t *c = g_room->clients[i];
        if (c == sender) continue;
        char mention[MAX_USERNAME_LEN + 2];
        snprintf(mention, sizeof(mention), "@%s", c->username);
        if (strstr(content, mention) != NULL) {
            client_addref(c);
            targets[target_count++] = c;
        }
    }
    pthread_rwlock_unlock(&g_room->lock);

    for (int i = 0; i < target_count; i++) {
        client_send(targets[i], "\a", 1);
        targets[i]->redraw_pending = true;
        client_release(targets[i]);
    }
}

/* Execute a command */

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
                    if (strncmp(input, "/me ", 4) == 0 && input[4] != '\0') {
                        msg.username[0] = '*';
                        msg.username[1] = '\0';
                        int n = snprintf(msg.content, sizeof(msg.content), "%s %s",
                                         client->username, input + 4);
                        if (n >= (int)sizeof(msg.content)) {
                            msg.content[sizeof(msg.content) - 1] = '\0';
                        }
                    } else {
                        snprintf(msg.username, sizeof(msg.username), "%s", client->username);
                        snprintf(msg.content, sizeof(msg.content), "%s", input);
                    }
                    room_broadcast(g_room, &msg);
                    notify_mentions(msg.content, client);
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

        case MODE_NORMAL: {
            int nm_msg_count = room_get_message_count(g_room);
            int nm_msg_height = client->height - 3;
            if (nm_msg_height < 1) nm_msg_height = 1;
            int nm_max_scroll = nm_msg_count - nm_msg_height;
            if (nm_max_scroll < 0) nm_max_scroll = 0;

            if (key == 'i') {
                client->mode = MODE_INSERT;
                tui_render_screen(client);
                return true;
            } else if (key == ':') {
                client->mode = MODE_COMMAND;
                client->command_input[0] = '\0';
                tui_render_screen(client);
                return true;
            } else if (key == 'j') {
                if (client->scroll_pos < nm_max_scroll) {
                    client->scroll_pos++;
                    tui_render_screen(client);
                }
                return true;
            } else if (key == 'k' && client->scroll_pos > 0) {
                client->scroll_pos--;
                tui_render_screen(client);
                return true;
            } else if (key == 4) {  /* Ctrl+D: half page down */
                int half = nm_msg_height / 2;
                if (half < 1) half = 1;
                client->scroll_pos += half;
                if (client->scroll_pos > nm_max_scroll) client->scroll_pos = nm_max_scroll;
                tui_render_screen(client);
                return true;
            } else if (key == 21) {  /* Ctrl+U: half page up */
                int half = nm_msg_height / 2;
                if (half < 1) half = 1;
                client->scroll_pos -= half;
                if (client->scroll_pos < 0) client->scroll_pos = 0;
                tui_render_screen(client);
                return true;
            } else if (key == 6) {  /* Ctrl+F: full page down */
                client->scroll_pos += nm_msg_height;
                if (client->scroll_pos > nm_max_scroll) client->scroll_pos = nm_max_scroll;
                tui_render_screen(client);
                return true;
            } else if (key == 2) {  /* Ctrl+B: full page up */
                client->scroll_pos -= nm_msg_height;
                if (client->scroll_pos < 0) client->scroll_pos = 0;
                tui_render_screen(client);
                return true;
            } else if (key == 'g') {
                client->scroll_pos = 0;
                tui_render_screen(client);
                return true;
            } else if (key == 'G') {
                client->scroll_pos = nm_max_scroll;
                tui_render_screen(client);
                return true;
            } else if (key == '?') {
                client->show_help = true;
                client->help_scroll_pos = 0;
                tui_render_help(client);
                return true;
            }
            break;
        }

        case MODE_COMMAND:
            if (key == 27) {  /* ESC - check for arrow key sequences */
                char seq[2];
                int n = ssh_channel_read_timeout(client->channel, seq, 1, 0, 50);
                if (n == 1 && seq[0] == '[') {
                    n = ssh_channel_read_timeout(client->channel, &seq[1], 1, 0, 50);
                    if (n == 1) {
                        if (seq[1] == 'A') {  /* Up arrow */
                            if (client->command_history_count > 0 &&
                                client->command_history_pos > 0) {
                                client->command_history_pos--;
                                strncpy(client->command_input,
                                        client->command_history[client->command_history_pos],
                                        sizeof(client->command_input) - 1);
                                client->command_input[sizeof(client->command_input) - 1] = '\0';
                                tui_render_screen(client);
                            }
                            return true;
                        } else if (seq[1] == 'B') {  /* Down arrow */
                            if (client->command_history_pos < client->command_history_count - 1) {
                                client->command_history_pos++;
                                strncpy(client->command_input,
                                        client->command_history[client->command_history_pos],
                                        sizeof(client->command_input) - 1);
                                client->command_input[sizeof(client->command_input) - 1] = '\0';
                            } else {
                                client->command_history_pos = client->command_history_count;
                                client->command_input[0] = '\0';
                            }
                            tui_render_screen(client);
                            return true;
                        }
                    }
                }
                client->mode = MODE_NORMAL;
                client->command_input[0] = '\0';
                tui_render_screen(client);
                return true;
            } else if (key == '\r' || key == '\n') {
                commands_dispatch(client);
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
    client->command_history_count = 0;
    client->command_history_pos = 0;
    client->connect_time = time(NULL);
    client->last_active = time(NULL);

    /* Check for exec command */
    if (client->exec_command[0] != '\0') {
        int exit_status = exec_dispatch(client);
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
    message_save(&join_msg);

    /* Show MOTD if motd.txt exists in state directory */
    {
        char motd_path[PATH_MAX];
        if (tnt_state_path(motd_path, sizeof(motd_path), "motd.txt") == 0) {
            FILE *motd_fp = fopen(motd_path, "r");
            if (motd_fp) {
                char motd_buf[sizeof(client->command_output) - 64];
                size_t motd_len = fread(motd_buf, 1, sizeof(motd_buf) - 1, motd_fp);
                fclose(motd_fp);
                if (motd_len > 0) {
                    motd_buf[motd_len] = '\0';
                    snprintf(client->command_output, sizeof(client->command_output),
                             "=== 公告 / MOTD ===\n%s", motd_buf);
                    tui_render_command_output(client);
                    seen_update_seq = room_get_update_seq(g_room);
                    goto main_loop;
                }
            }
        }
    }

    /* Render initial screen */
    tui_render_screen(client);
    seen_update_seq = room_get_update_seq(g_room);

main_loop:

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

            if (g_idle_timeout > 0 && joined_room &&
                time(NULL) - client->last_active >= g_idle_timeout) {
                client_printf(client, "\r\n\033[33mDisconnected: idle timeout (%d min)\033[0m\r\n",
                              g_idle_timeout / 60);
                break;
            }
            continue;
        }

        int n = ssh_channel_read(client->channel, buf, 1, 0);

        if (n <= 0) {
            /* EOF or error */
            break;
        }

        last_keepalive = time(NULL);
        client->last_active = last_keepalive;

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
                } else if (b >= 128) {  /* UTF-8 multi-byte */
                    int char_len = utf8_byte_length(b);
                    if (char_len <= 0 || char_len > 4) continue;
                    buf[0] = b;
                    if (char_len > 1) {
                        int read_bytes = ssh_channel_read_timeout(
                            client->channel, &buf[1], char_len - 1, 0, 5000);
                        if (read_bytes != char_len - 1) continue;
                    }
                    if (!utf8_is_valid_sequence(buf, char_len)) continue;
                    size_t len = strlen(client->command_input);
                    if (len + (size_t)char_len < sizeof(client->command_input) - 1) {
                        memcpy(client->command_input + len, buf, char_len);
                        client->command_input[len + char_len] = '\0';
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
        message_save(&leave_msg);
    }

    ratelimit_release_ip(client->client_ip);

    /* Remove channel callbacks before releasing refs to prevent use-after-free
     * if a callback fires between the two releases. */
    if (client->channel && client->channel_cb) {
        ssh_remove_channel_callbacks(client->channel, client->channel_cb);
    }

    /* Release the callback reference (paired with addref before install_client_channel_callbacks) */
    client_release(client);

    /* Release the main reference - client will be freed when all refs are gone */
    client_release(client);

    /* Decrement connection count */
    ratelimit_decrement_total();

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
        ratelimit_record_auth_failure(ctx->client_ip);
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
            ratelimit_record_auth_failure(ctx->client_ip);
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

    /* SSH_PUBLICKEY_STATE_NONE = key offer (no signature yet).
     * Return SUCCESS to tell libssh "I accept this key, verify the signature."
     * SSH_PUBLICKEY_STATE_VALID = signature verified by libssh. */
    if (signature_state != SSH_PUBLICKEY_STATE_VALID) {
        return SSH_AUTH_SUCCESS;
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
        ratelimit_release_ip(ctx->client_ip);
    }
    destroy_session_context(ctx);
    ratelimit_decrement_total();
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

    int w = width;
    int h = height;
    sanitize_terminal_size(&w, &h);
    client->width = w;
    client->height = h;
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
        ratelimit_release_ip(accepted_ip);
        ssh_disconnect(session);
        ssh_free(session);
        ratelimit_decrement_total();
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

        if (time(NULL) - start_time > 10) {
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
    int init_w = ctx->pty_width;
    int init_h = ctx->pty_height;
    sanitize_terminal_size(&init_w, &init_h);
    client->width = init_w;
    client->height = init_h;
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
    /* Initialize rate-limit / connection-count subsystem */
    ratelimit_init();

    /* Auth / session config (will move into auth.c and input.c in later PR2 steps) */
    g_idle_timeout = env_int("TNT_IDLE_TIMEOUT", DEFAULT_IDLE_TIMEOUT, 0, 86400);
    const char *token_env = getenv("TNT_ACCESS_TOKEN");
    if (token_env != NULL) {
        strncpy(g_access_token, token_env, sizeof(g_access_token) - 1);
        g_access_token[sizeof(g_access_token) - 1] = '\0';
    }
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
        if (!ratelimit_check_and_increment_total()) {
            fprintf(stderr, "Max connections reached, rejecting %s\n", client_ip);
            ssh_disconnect(session);
            ssh_free(session);
            continue;
        }

        if (!ratelimit_check_ip(client_ip)) {
            ratelimit_decrement_total();
            ssh_disconnect(session);
            ssh_free(session);
            continue;
        }

        accepted = calloc(1, sizeof(*accepted));
        if (!accepted) {
            ratelimit_release_ip(client_ip);
            ratelimit_decrement_total();
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
            ratelimit_release_ip(client_ip);
            ratelimit_decrement_total();
            ssh_disconnect(session);
            ssh_free(session);
            continue;
        }
    }
    /* Unreachable — the while(1) loop only exits via signal/_exit(). */
}
