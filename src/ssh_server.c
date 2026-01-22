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
    client_printf(client, "请输入用户名: ");

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
                client_send(client, &b, 1);
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
            } else if (key == '\r') {  /* Enter */
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
                int max_scroll = room_get_message_count(g_room) - 1;
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
                client->scroll_pos = room_get_message_count(g_room) - 1;
                if (client->scroll_pos < 0) client->scroll_pos = 0;
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

        /* Ctrl+C */
        if (b == 3) break;

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

    return NULL;
}

/* Handle SSH authentication */
static int handle_auth(ssh_session session) {
    ssh_message message;

    do {
        message = ssh_message_get(session);
        if (!message) break;

        if (ssh_message_type(message) == SSH_REQUEST_AUTH) {
            if (ssh_message_subtype(message) == SSH_AUTH_METHOD_PASSWORD) {
                /* Accept any password for simplicity */
                /* In production, you'd want to verify against a user database */
                ssh_message_auth_reply_success(message, 0);
                ssh_message_free(message);
                return 0;
            } else if (ssh_message_subtype(message) == SSH_AUTH_METHOD_NONE) {
                /* Accept passwordless authentication for open chatroom */
                ssh_message_auth_reply_success(message, 0);
                ssh_message_free(message);
                return 0;
            }
        }

        ssh_message_reply_default(message);
        ssh_message_free(message);
    } while (1);

    return -1;
}

/* Handle SSH channel requests */
static ssh_channel handle_channel_open(ssh_session session) {
    ssh_message message;
    ssh_channel channel = NULL;

    do {
        message = ssh_message_get(session);
        if (!message) break;

        if (ssh_message_type(message) == SSH_REQUEST_CHANNEL_OPEN &&
            ssh_message_subtype(message) == SSH_CHANNEL_SESSION) {
            channel = ssh_message_channel_request_open_reply_accept(message);
            ssh_message_free(message);
            return channel;
        }

        ssh_message_reply_default(message);
        ssh_message_free(message);
    } while (1);

    return NULL;
}

/* Handle PTY request and get terminal size */
static int handle_pty_request(ssh_channel channel, client_t *client) {
    ssh_message message;
    int pty_received = 0;
    int shell_received = 0;

    do {
        message = ssh_message_get(ssh_channel_get_session(channel));
        if (!message) break;

        if (ssh_message_type(message) == SSH_REQUEST_CHANNEL) {
            if (ssh_message_subtype(message) == SSH_CHANNEL_REQUEST_PTY) {
                /* Get terminal dimensions from PTY request */
                client->width = ssh_message_channel_request_pty_width(message);
                client->height = ssh_message_channel_request_pty_height(message);

                /* Default to 80x24 if invalid */
                if (client->width <= 0 || client->width > 500) client->width = 80;
                if (client->height <= 0 || client->height > 200) client->height = 24;

                ssh_message_channel_request_reply_success(message);
                ssh_message_free(message);
                pty_received = 1;

                /* Don't return yet, wait for shell request */
                if (shell_received) {
                    return 0;
                }
                continue;

            } else if (ssh_message_subtype(message) == SSH_CHANNEL_REQUEST_SHELL) {
                ssh_message_channel_request_reply_success(message);
                ssh_message_free(message);
                shell_received = 1;

                /* If we got PTY, we're done */
                if (pty_received) {
                    return 0;
                }
                continue;

            } else if (ssh_message_subtype(message) == SSH_CHANNEL_REQUEST_WINDOW_CHANGE) {
                /* Handle terminal resize - this should be handled during session, not here */
                /* For now, just acknowledge and ignore during init */
                ssh_message_channel_request_reply_success(message);
                ssh_message_free(message);
                continue;
            }
        }

        ssh_message_reply_default(message);
        ssh_message_free(message);
    } while (!pty_received || !shell_received);

    return (pty_received && shell_received) ? 0 : -1;
}

/* Initialize SSH server */
int ssh_server_init(int port) {
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

        /* Perform key exchange */
        if (ssh_handle_key_exchange(session) != SSH_OK) {
            fprintf(stderr, "Key exchange failed: %s\n", ssh_get_error(session));
            ssh_disconnect(session);
            ssh_free(session);
            continue;
        }

        /* Handle authentication */
        if (handle_auth(session) < 0) {
            fprintf(stderr, "Authentication failed\n");
            ssh_disconnect(session);
            ssh_free(session);
            continue;
        }

        /* Open channel */
        ssh_channel channel = handle_channel_open(session);
        if (!channel) {
            fprintf(stderr, "Failed to open channel\n");
            ssh_disconnect(session);
            ssh_free(session);
            continue;
        }

        /* Create client structure */
        client_t *client = calloc(1, sizeof(client_t));
        if (!client) {
            ssh_channel_close(channel);
            ssh_channel_free(channel);
            ssh_disconnect(session);
            ssh_free(session);
            continue;
        }

        client->session = session;
        client->channel = channel;
        client->fd = -1;  /* Not used with SSH */
        client->ref_count = 1;  /* Initial reference */
        pthread_mutex_init(&client->ref_lock, NULL);

        /* Handle PTY request and get terminal size */
        if (handle_pty_request(channel, client) < 0) {
            /* Set defaults if PTY request fails */
            client->width = 80;
            client->height = 24;
        }

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
