#include "input.h"
#include "chat_room.h"
#include "client.h"
#include "commands.h"
#include "common.h"
#include "exec.h"
#include "history_view.h"
#include "i18n.h"
#include "message.h"
#include "ratelimit.h"
#include "system_message.h"
#include "tui.h"
#include "utf8.h"
#include <libssh/callbacks.h>
#include <libssh/libssh.h>
#include <libssh/server.h>
#include <strings.h>  /* strncasecmp */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_idle_timeout = DEFAULT_IDLE_TIMEOUT;
static help_lang_t g_default_lang = LANG_EN;

void input_init(void) {
    g_idle_timeout = env_int("TNT_IDLE_TIMEOUT", DEFAULT_IDLE_TIMEOUT, 0, 86400);
    g_default_lang = i18n_default_lang();
}

static int read_username(client_t *client) {
    char username[MAX_USERNAME_LEN] = {0};
    int pos = 0;
    char buf[4];

    tui_render_welcome(client);
    client_printf(client, "%s", i18n_text(client->help_lang,
                                          I18N_USERNAME_PROMPT));

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
            client_printf(client, "%s", i18n_text(client->help_lang,
                                                  I18N_INVALID_USERNAME));
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
        targets[i]->unread_mentions++;
        targets[i]->redraw_pending = true;
        client_release(targets[i]);
    }
}

static int read_channel_exact(client_t *client, char *buf, size_t len,
                              int timeout_ms) {
    size_t got = 0;

    while (got < len) {
        int n = ssh_channel_read_timeout(client->channel, buf + got,
                                         len - got, 0, timeout_ms);
        if (n == SSH_AGAIN || n <= 0) {
            break;
        }
        got += (size_t)n;
    }

    return (int)got;
}

static bool append_paste_byte(char *input, unsigned char b) {
    if (b == '\r' || b == '\n' || b == '\t') {
        b = ' ';
    }
    if (b < 32) {
        return true;
    }

    size_t cur = strlen(input);
    if (cur < MAX_MESSAGE_LEN - 1) {
        input[cur] = (char)b;
        input[cur + 1] = '\0';
        return true;
    }

    return false;
}

static void normal_scroll_to_latest(client_t *client) {
    if (!client) return;
    history_view_scroll_to_latest(&client->scroll_pos, &client->follow_tail,
                                  room_get_message_count(g_room),
                                  history_view_height(client->height));
}

static void normal_scroll_by(client_t *client, int delta) {
    if (!client) return;
    history_view_scroll_by(&client->scroll_pos, &client->follow_tail,
                           room_get_message_count(g_room),
                           history_view_height(client->height), delta);
}

/* Handle a single key press.  Returns true if the key was fully consumed
 * (no further character buffering needed). */
static bool handle_key(client_t *client, unsigned char key, char *input) {
    /* Handle Ctrl+C (Exit or switch to NORMAL) */
    if (key == 3) {
        client_mode_t previous_mode = client->mode;
        if (previous_mode != MODE_NORMAL) {
            client->mode = MODE_NORMAL;
            client->command_input[0] = '\0';
            client->show_help = false;
            if (previous_mode == MODE_INSERT) {
                normal_scroll_to_latest(client);
            }
            tui_render_screen(client);
        } else {
            /* In NORMAL mode, Ctrl+C exits */
            client->connected = false;
        }
        return true;
    }

    /* Handle help screen */
    if (client->show_help) {
        /* Page size: roughly the visible help body region. */
        int page = client->height - 2;
        if (page < 1) page = 1;
        int half = page / 2;
        if (half < 1) half = 1;

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
        } else if (key == 4) {  /* Ctrl+D: half page down */
            client->help_scroll_pos += half;
            tui_render_help(client);
        } else if (key == 21) {  /* Ctrl+U: half page up */
            client->help_scroll_pos -= half;
            if (client->help_scroll_pos < 0) client->help_scroll_pos = 0;
            tui_render_help(client);
        } else if (key == 6) {  /* Ctrl+F: full page down */
            client->help_scroll_pos += page;
            tui_render_help(client);
        } else if (key == 2) {  /* Ctrl+B: full page up */
            client->help_scroll_pos -= page;
            if (client->help_scroll_pos < 0) client->help_scroll_pos = 0;
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
        bool was_motd = client->show_motd;
        client->command_output[0] = '\0';
        client->show_motd = false;
        client->mode = MODE_NORMAL;
        if (was_motd) {
            normal_scroll_to_latest(client);
        }
        tui_render_screen(client);
        return true;  /* Key consumed */
    }

    /* Mode-specific handling */
    switch (client->mode) {
        case MODE_INSERT:
            if (key == 27) {  /* ESC — may also be the start of an arrow seq */
                char seq[2];
                int n = ssh_channel_read_timeout(client->channel, seq, 1, 0, 50);
                if (n == 1 && seq[0] == '[') {
                    n = ssh_channel_read_timeout(client->channel, &seq[1], 1, 0, 50);
                    if (n == 1) {
                        if (seq[1] == 'A') {  /* Up — walk back through sent history */
                            if (client->insert_history_count > 0 &&
                                client->insert_history_pos > 0) {
                                client->insert_history_pos--;
                                strncpy(input,
                                        client->insert_history[client->insert_history_pos],
                                        MAX_MESSAGE_LEN - 1);
                                input[MAX_MESSAGE_LEN - 1] = '\0';
                                tui_render_input(client, input);
                            }
                            return true;
                        } else if (seq[1] == 'B') {  /* Down — walk forward */
                            if (client->insert_history_pos <
                                client->insert_history_count - 1) {
                                client->insert_history_pos++;
                                strncpy(input,
                                        client->insert_history[client->insert_history_pos],
                                        MAX_MESSAGE_LEN - 1);
                                input[MAX_MESSAGE_LEN - 1] = '\0';
                            } else {
                                client->insert_history_pos =
                                    client->insert_history_count;
                                input[0] = '\0';
                            }
                            tui_render_input(client, input);
                            return true;
                        } else if (seq[1] == '2') {
                            /* Could be bracketed-paste start "ESC[200~".
                             * Read the next 3 bytes and confirm. */
                            char rest[3];
                            int m = read_channel_exact(client, rest,
                                                       sizeof(rest), 500);
                            if (m == 3 && rest[0] == '0' && rest[1] == '0'
                                       && rest[2] == '~') {
                                /* Drain bytes into `input` until we see
                                 * the end marker ESC[201~.  Newlines become
                                 * spaces so a multi-line paste stays a
                                 * single message instead of N sends. */
                                bool overflow = false;
                                while (1) {
                                    char b;
                                    int k = ssh_channel_read_timeout(
                                        client->channel, &b, 1, 0, 5000);
                                    if (k != 1) break;
                                    if (b == '\033') {
                                        char tail[5];
                                        int t = read_channel_exact(
                                            client, tail, sizeof(tail), 500);
                                        if (t == 5 && tail[0] == '['
                                                && tail[1] == '2'
                                                && tail[2] == '0'
                                                && tail[3] == '1'
                                                && tail[4] == '~') {
                                            break;  /* end of paste */
                                        }
                                        /* Stray ESC inside paste: drop the ESC
                                         * but keep printable bytes that
                                         * followed it. */
                                        for (int i = 0; i < t; i++) {
                                            if (!append_paste_byte(
                                                    input,
                                                    (unsigned char)tail[i])) {
                                                overflow = true;
                                            }
                                        }
                                        continue;
                                    }
                                    if (!append_paste_byte(input,
                                                           (unsigned char)b)) {
                                        overflow = true;
                                    }
                                }
                                tui_render_input(client, input);
                                if (overflow) {
                                    client_send(client, "\a", 1);
                                }
                            }
                            return true;
                        }
                    }
                }
                /* Plain ESC — fall through to NORMAL mode */
                client->mode = MODE_NORMAL;
                normal_scroll_to_latest(client);
                tui_render_screen(client);
                return true;
            } else if (key == '\r' || key == '\n') {  /* Enter */
                if (input[0] != '\0') {
                    /* Record into the per-client INSERT history ring */
                    int max_hist = (int)(sizeof(client->insert_history) /
                                         sizeof(client->insert_history[0]));
                    if (client->insert_history_count >= max_hist) {
                        memmove(&client->insert_history[0],
                                &client->insert_history[1],
                                (max_hist - 1) * sizeof(client->insert_history[0]));
                        client->insert_history_count = max_hist - 1;
                    }
                    snprintf(client->insert_history[client->insert_history_count],
                             sizeof(client->insert_history[0]), "%s", input);
                    client->insert_history_count++;
                    client->insert_history_pos = client->insert_history_count;

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
            } else if (key == 9) { /* Tab: complete @mention */
                /* Walk back from end to find the start of the trailing
                 * "@…" token (an '@' not preceded by an alphanumeric).
                 * If found, scan g_room for the first case-insensitive
                 * username prefix-match (cycling past self) and replace
                 * the token. */
                size_t in_len = strlen(input);
                ssize_t at_idx = -1;
                for (ssize_t i = (ssize_t)in_len - 1; i >= 0; i--) {
                    unsigned char c = (unsigned char)input[i];
                    if (c == '@') {
                        if (i == 0 || input[i - 1] == ' ') at_idx = i;
                        break;
                    }
                    if (c == ' ') break;
                }
                if (at_idx >= 0) {
                    const char *prefix = input + at_idx + 1;
                    size_t plen = strlen(prefix);
                    char match[MAX_USERNAME_LEN] = "";
                    pthread_rwlock_rdlock(&g_room->lock);
                    for (int i = 0; i < g_room->client_count; i++) {
                        const char *uname = g_room->clients[i]->username;
                        if (plen == 0
                                ? strcmp(uname, client->username) != 0
                                : strncasecmp(uname, prefix, plen) == 0) {
                            snprintf(match, sizeof(match), "%s", uname);
                            break;
                        }
                    }
                    pthread_rwlock_unlock(&g_room->lock);
                    if (match[0] != '\0') {
                        /* Replace "@<prefix>" with "@<match> " (trailing
                         * space so the next word starts cleanly). */
                        size_t avail = MAX_MESSAGE_LEN - 1
                                       - (size_t)at_idx - 1;
                        size_t mlen = strlen(match);
                        if (mlen + 1 <= avail) {
                            input[at_idx + 1] = '\0';
                            strncat(input, match, avail);
                            strncat(input, " ", 1);
                            tui_render_input(client, input);
                        }
                    }
                }
                return true;
            }
            break;

        case MODE_NORMAL: {
            int nm_msg_height = history_view_height(client->height);

            if (key == 'i') {
                client->mode = MODE_INSERT;
                client->follow_tail = true;
                client->unread_mentions = 0;
                tui_render_screen(client);
                return true;
            } else if (key == ':') {
                client->mode = MODE_COMMAND;
                client->command_input[0] = '\0';
                tui_render_screen(client);
                return true;
            } else if (key == 'j') {
                normal_scroll_by(client, 1);
                tui_render_screen(client);
                return true;
            } else if (key == 'k' && client->scroll_pos > 0) {
                normal_scroll_by(client, -1);
                tui_render_screen(client);
                return true;
            } else if (key == 4) {  /* Ctrl+D: half page down */
                int half = nm_msg_height / 2;
                if (half < 1) half = 1;
                normal_scroll_by(client, half);
                tui_render_screen(client);
                return true;
            } else if (key == 21) {  /* Ctrl+U: half page up */
                int half = nm_msg_height / 2;
                if (half < 1) half = 1;
                normal_scroll_by(client, -half);
                tui_render_screen(client);
                return true;
            } else if (key == 6) {  /* Ctrl+F: full page down */
                normal_scroll_by(client, nm_msg_height);
                tui_render_screen(client);
                return true;
            } else if (key == 2) {  /* Ctrl+B: full page up */
                normal_scroll_by(client, -nm_msg_height);
                tui_render_screen(client);
                return true;
            } else if (key == 'g') {
                history_view_scroll_to_oldest(&client->scroll_pos,
                                              &client->follow_tail);
                tui_render_screen(client);
                return true;
            } else if (key == 'G') {
                normal_scroll_to_latest(client);
                client->unread_mentions = 0;
                tui_render_screen(client);
                return true;
            } else if (key == 27) {
                char seq[4];
                int n = ssh_channel_read_timeout(client->channel, seq, 1, 0, 50);
                if (n == 1 && seq[0] == '[') {
                    n = ssh_channel_read_timeout(client->channel, &seq[1], 1, 0, 50);
                    if (n == 1) {
                        if (seq[1] == 'A') {          /* Up arrow */
                            normal_scroll_by(client, -1);
                        } else if (seq[1] == 'B') {   /* Down arrow */
                            normal_scroll_by(client, 1);
                        } else if (seq[1] == 'H') {   /* Home */
                            history_view_scroll_to_oldest(&client->scroll_pos,
                                                          &client->follow_tail);
                        } else if (seq[1] == 'F') {   /* End */
                            normal_scroll_to_latest(client);
                        } else if (seq[1] >= '1' && seq[1] <= '6') {
                            n = ssh_channel_read_timeout(client->channel,
                                                         &seq[2], 1, 0, 50);
                            if (n == 1 && seq[2] == '~') {
                                if (seq[1] == '5') {        /* PageUp */
                                    normal_scroll_by(client, -nm_msg_height);
                                } else if (seq[1] == '6') { /* PageDown */
                                    normal_scroll_by(client, nm_msg_height);
                                } else if (seq[1] == '1') { /* Home */
                                    history_view_scroll_to_oldest(
                                        &client->scroll_pos,
                                        &client->follow_tail);
                                } else if (seq[1] == '4') { /* End */
                                    normal_scroll_to_latest(client);
                                }
                            }
                        }
                        tui_render_screen(client);
                    }
                }
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
    bool bracketed_paste_enabled = false;
    uint64_t seen_update_seq;
    time_t last_keepalive = time(NULL);

    /* Terminal size already set from PTY request */
    client->mode = MODE_INSERT;
    client->follow_tail = true;
    client->help_lang = g_default_lang;
    client->connected = true;
    client->command_history_count = 0;
    client->command_history_pos = 0;
    client->connect_time = time(NULL);
    client->last_active = time(NULL);

    /* Check for exec command */
    if (client->exec_command[0] != '\0') {
        int exit_status = exec_dispatch(client);
        ssh_channel_request_send_exit_status(client->channel, exit_status);
        ssh_channel_send_eof(client->channel);
        ssh_blocking_flush(client->session, 1000);
        ssh_channel_close(client->channel);
        goto cleanup;
    }

    /* Read username */
    if (read_username(client) < 0) {
        goto cleanup;
    }

    /* Add to room */
    if (room_add_client(g_room, client) < 0) {
        client_printf(client, "%s", i18n_text(client->help_lang,
                                              I18N_ROOM_FULL));
        goto cleanup;
    }
    joined_room = true;

    /* Enable xterm bracketed-paste mode only for interactive chat, so
     * multi-line pastes arrive framed by ESC[200~...ESC[201~ instead of
     * as a stream of Enters.  Terminals that don't recognise it ignore it. */
    client_send(client, "\033[?2004h", 8);
    bracketed_paste_enabled = true;

    /* Broadcast join message */
    message_t join_msg;
    system_message_make_join(&join_msg, client->username, client->help_lang);
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
                    if (room_updated && client->mode == MODE_NORMAL &&
                        client->follow_tail) {
                        normal_scroll_to_latest(client);
                    }
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
                client_printf(client,
                              i18n_text(client->help_lang,
                                        I18N_IDLE_TIMEOUT_FORMAT),
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
                    } else {
                        client_send(client, "\a", 1);
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
                    if (len + char_len <= MAX_MESSAGE_LEN - 1) {
                        memcpy(input + len, buf, char_len);
                        input[len + char_len] = '\0';
                        tui_render_input(client, input);
                    } else {
                        client_send(client, "\a", 1);
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
    if (bracketed_paste_enabled && client->channel &&
        ssh_channel_is_open(client->channel)) {
        client_send(client, "\033[?2004l", 8);
    }

    /* Broadcast leave message */
    if (joined_room) {
        message_t leave_msg;
        system_message_make_leave(&leave_msg, client->username,
                                  client->help_lang);

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
