#include "ssh_server.h"
#include "tui.h"
#include "utf8.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>

/* Send data to client */
int client_send(client_t *client, const char *data, size_t len) {
    if (!client || !client->connected) return -1;
    ssize_t sent = write(client->fd, data, len);
    return (sent < 0) ? -1 : 0;
}

/* Send formatted string to client */
int client_printf(client_t *client, const char *fmt, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return client_send(client, buffer, len);
}

/* Read username from client */
static int read_username(client_t *client) {
    char username[MAX_USERNAME_LEN] = {0};
    int pos = 0;
    unsigned char buf[4];

    tui_clear_screen(client->fd);
    client_printf(client, "请输入用户名: ");

    while (1) {
        ssize_t n = read(client->fd, buf, 1);
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
                write(client->fd, &b, 1);
            }
        } else {
            /* UTF-8 multi-byte */
            int len = utf8_byte_length(b);
            buf[0] = b;
            if (len > 1) {
                read(client->fd, &buf[1], len - 1);
            }
            if (pos + len < MAX_USERNAME_LEN - 1) {
                memcpy(username + pos, buf, len);
                pos += len;
                username[pos] = '\0';
                write(client->fd, buf, len);
            }
        }
    }

    client_printf(client, "\n");

    if (username[0] == '\0') {
        strcpy(client->username, "anonymous");
    } else {
        strncpy(client->username, username, MAX_USERNAME_LEN - 1);
        /* Truncate to 20 characters */
        if (utf8_strlen(client->username) > 20) {
            utf8_truncate(client->username, 20);
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

/* Handle client key press */
static void handle_key(client_t *client, unsigned char key, char *input) {
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
        return;
    }

    /* Handle command output display */
    if (client->command_output[0] != '\0') {
        client->command_output[0] = '\0';
        client->mode = MODE_NORMAL;
        tui_render_screen(client);
        return;
    }

    /* Mode-specific handling */
    switch (client->mode) {
        case MODE_INSERT:
            if (key == 27) {  /* ESC */
                client->mode = MODE_NORMAL;
                client->scroll_pos = 0;
                tui_render_screen(client);
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
            } else if (key == 127 || key == 8) {  /* Backspace */
                if (input[0] != '\0') {
                    utf8_remove_last_char(input);
                    tui_render_input(client, input);
                }
            }
            break;

        case MODE_NORMAL:
            if (key == 'i') {
                client->mode = MODE_INSERT;
                tui_render_screen(client);
            } else if (key == ':') {
                client->mode = MODE_COMMAND;
                client->command_input[0] = '\0';
                tui_render_screen(client);
            } else if (key == 'j') {
                int max_scroll = room_get_message_count(g_room) - 1;
                if (client->scroll_pos < max_scroll) {
                    client->scroll_pos++;
                    tui_render_screen(client);
                }
            } else if (key == 'k' && client->scroll_pos > 0) {
                client->scroll_pos--;
                tui_render_screen(client);
            } else if (key == 'g') {
                client->scroll_pos = 0;
                tui_render_screen(client);
            } else if (key == 'G') {
                client->scroll_pos = room_get_message_count(g_room) - 1;
                if (client->scroll_pos < 0) client->scroll_pos = 0;
                tui_render_screen(client);
            } else if (key == '?') {
                client->show_help = true;
                client->help_scroll_pos = 0;
                tui_render_help(client);
            }
            break;

        case MODE_COMMAND:
            if (key == 27) {  /* ESC */
                client->mode = MODE_NORMAL;
                client->command_input[0] = '\0';
                tui_render_screen(client);
            } else if (key == '\r' || key == '\n') {
                execute_command(client);
            } else if (key == 127 || key == 8) {  /* Backspace */
                if (client->command_input[0] != '\0') {
                    utf8_remove_last_char(client->command_input);
                    tui_render_screen(client);
                }
            }
            break;

        default:
            break;
    }
}

/* Handle client session */
void* client_handle_session(void *arg) {
    client_t *client = (client_t*)arg;
    char input[MAX_MESSAGE_LEN] = {0};
    unsigned char buf[4];

    /* Get terminal size (assume 80x24 for telnet) */
    client->width = 80;
    client->height = 24;
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
    strcpy(join_msg.username, "系统");
    snprintf(join_msg.content, MAX_MESSAGE_LEN, "%s 加入了聊天室", client->username);
    room_broadcast(g_room, &join_msg);

    /* Render initial screen */
    tui_render_screen(client);

    /* Main input loop */
    while (client->connected) {
        ssize_t n = read(client->fd, buf, 1);
        if (n <= 0) break;

        unsigned char b = buf[0];

        /* Ctrl+C */
        if (b == 3) break;

        /* Handle special keys */
        handle_key(client, b, input);

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
                buf[0] = b;
                if (char_len > 1) {
                    read(client->fd, &buf[1], char_len - 1);
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
                int len = strlen(client->command_input);
                if (len < sizeof(client->command_input) - 1) {
                    client->command_input[len] = b;
                    client->command_input[len + 1] = '\0';
                    tui_render_screen(client);
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
        strcpy(leave_msg.username, "系统");
        snprintf(leave_msg.content, MAX_MESSAGE_LEN, "%s 离开了聊天室", client->username);

        client->connected = false;
        room_remove_client(g_room, client);
        room_broadcast(g_room, &leave_msg);
    }

    close(client->fd);
    free(client);

    return NULL;
}

/* Initialize server socket */
int ssh_server_init(int port) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return -1;
    }

    /* Set socket options */
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return -1;
    }

    if (listen(listen_fd, 10) < 0) {
        perror("listen");
        close(listen_fd);
        return -1;
    }

    return listen_fd;
}

/* Start server (blocking) */
int ssh_server_start(int listen_fd) {
    printf("TNT chat server listening on port %d\n", DEFAULT_PORT);
    printf("Connect with: telnet localhost %d\n", DEFAULT_PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        /* Create client structure */
        client_t *client = calloc(1, sizeof(client_t));
        if (!client) {
            close(client_fd);
            continue;
        }

        client->fd = client_fd;

        /* Create thread for client */
        pthread_t thread;
        if (pthread_create(&thread, NULL, client_handle_session, client) != 0) {
            close(client_fd);
            free(client);
            continue;
        }

        pthread_detach(thread);
    }

    return 0;
}
