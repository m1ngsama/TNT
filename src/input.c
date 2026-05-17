#include "input.h"
#include "chat_room.h"
#include "client.h"
#include "commands.h"
#include "common.h"
#include "exec.h"
#include "message.h"
#include "ratelimit.h"
#include "tui.h"
#include "utf8.h"
#include <libssh/callbacks.h>
#include <libssh/libssh.h>
#include <libssh/server.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_idle_timeout = DEFAULT_IDLE_TIMEOUT;

void input_init(void) {
    g_idle_timeout = env_int("TNT_IDLE_TIMEOUT", DEFAULT_IDLE_TIMEOUT, 0, 86400);
}

static int read_username(client_t *client) {
    char username[MAX_USERNAME_LEN] = {0};
    int pos = 0;
    char buf[4];

    tui_render_welcome(client);
    client_printf(client, "  请输入用户名 (留空 anonymous): ");

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

/* Handle a single key press.  Returns true if the key was fully consumed
 * (no further character buffering needed). */
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

    /* Handle command output / MOTD display: any key dismisses */
    if (client->command_output[0] != '\0') {
        client->command_output[0] = '\0';
        client->show_motd = false;
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

void input_run_session(client_t *client) {
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
                    snprintf(client->command_output,
                             sizeof(client->command_output),
                             "%s", motd_buf);
                    client->show_motd = true;
                    tui_render_motd(client);
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
                } else if (client->show_motd) {
                    tui_render_motd(client);
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

    /* Release the callback reference (paired with addref before client_install_channel_callbacks) */
    client_release(client);

    /* Release the main reference - client will be freed when all refs are gone */
    client_release(client);

    /* Decrement connection count */
    ratelimit_decrement_total();
}
