#if !defined(__APPLE__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE /* ppoll() on Linux */
#endif

#include "input.h"
#include "chat_room.h"
#include "client.h"
#include "command_catalog.h"
#include "commands.h"
#include "config_defaults.h"
#include "common.h"
#include "editor.h"
#include "exec.h"
#include "history_view.h"
#include "i18n.h"
#include "keymap.h"
#include "input_buffer.h"
#include "message.h"
#include "module_runtime.h"
#include "ratelimit.h"
#include "system_message.h"
#include "theme.h"
#include "tui.h"
#include "utf8.h"
#include <libssh/callbacks.h>
#include <libssh/libssh.h>
#include <libssh/server.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <strings.h>  /* strncasecmp */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_idle_timeout = TNT_DEFAULT_IDLE_TIMEOUT;
static ui_lang_t g_default_ui_lang = UI_LANG_EN;
/* Server-wide keymap default.  Still vim, so this commit changes nothing a
 * user sees; the flip is its own commit. */
static tnt_keymap_t g_default_keymap = TNT_KEYMAP_VIM;

#define KEEPALIVE_INTERVAL_MS 15000
#define DARWIN_HIGH_FD_POLL_MS 10
#define CHANNEL_CLOSE_ACK_TIMEOUT_MS 1000
#define ROOM_REDRAW_MIN_INTERVAL_MS 8
#define USERNAME_TIMEOUT_MS 60000

static int64_t input_monotonic_millis(void);

static const char *input_client_name(const struct client *client) {
    return client ? ((const client_t *)client)->username : NULL;
}

void input_init(void) {
    g_idle_timeout = tnt_config_env_int(&TNT_CONFIG_IDLE_TIMEOUT);
    g_default_ui_lang = i18n_default_ui_lang();
    g_default_keymap = tnt_keymap_from_name(getenv("TNT_KEYMAP"),
                                            g_default_keymap);
    room_set_client_notifier(g_room, client_wake);
    room_set_client_name_accessor(g_room, input_client_name);
}

static int read_username(client_t *client) {
    char username[MAX_USERNAME_LEN] = {0};
    int pos = 0;
    char buf[4];
    const char *prompt = i18n_text(client->ui_lang, I18N_USERNAME_PROMPT);
    int64_t deadline_ms = input_monotonic_millis() + USERNAME_TIMEOUT_MS;

    tui_render_welcome(client);
    client_printf(client, "%s", prompt);

    while (1) {
        int64_t remaining = deadline_ms - input_monotonic_millis();
        int n;

        if (remaining <= 0) {
            return -1;
        }

        n = ssh_channel_read_timeout(client->channel, buf, 1, 0,
                                     (int)remaining);

        if (n == SSH_AGAIN) {
            /* Signals can wake libssh before the requested timeout.  Keep the
             * original absolute deadline rather than granting a fresh minute. */
            if (!ssh_channel_is_open(client->channel)) {
                return -1;
            }
            if (input_monotonic_millis() >= deadline_ms) {
                return -1;
            }
            continue;
        }

        if (n <= 0) return -1;

        unsigned char b = buf[0];

        if (b == '\r' || b == '\n') {
            break;
        } else if (b == 3 || b == 4) {  /* Ctrl+C / Ctrl+D */
            return -1;
        } else if (b == 21) {  /* Ctrl+U: clear line */
            username[0] = '\0';
            pos = 0;
            client_printf(client, "\r\033[K%s", prompt);
        } else if (b == 23) {  /* Ctrl+W: delete word */
            if (username[0] != '\0') {
                pos = (int)utf8_remove_last_word(username, (size_t)pos);
                client_printf(client, "\r\033[K%s%s", prompt, username);
            }
        } else if (b == 127 || b == 8) {  /* Backspace */
            if (pos > 0) {
                /* Compute width of the last character before removing it */
                int ci = pos - 1;
                while (ci > 0 && (username[ci] & 0xC0) == 0x80) ci--;
                int bytes_read;
                uint32_t cp = utf8_decode(username + ci, &bytes_read);
                int w = utf8_char_width(cp);
                pos = (int)utf8_remove_last_char(username, (size_t)pos);
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
                int64_t continuation_remaining =
                    deadline_ms - input_monotonic_millis();
                int continuation_timeout;
                int read_bytes;

                if (continuation_remaining <= 0) {
                    return -1;
                }
                continuation_timeout = continuation_remaining > 5000
                                           ? 5000
                                           : (int)continuation_remaining;
                read_bytes = ssh_channel_read_timeout(
                    client->channel, &buf[1], len - 1, 0,
                    continuation_timeout);
                if (read_bytes != len - 1) {
                    /* Incomplete or timed-out UTF-8 continuation */
                    if (input_monotonic_millis() >= deadline_ms) {
                        return -1;
                    }
                    continue;
                }
            }
            /* Validate the complete UTF-8 sequence */
            if (!utf8_is_valid_sequence(buf, len)) {
                continue;
            }
            if (utf8_is_control_sequence(buf, len)) {
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
            client_printf(client, "%s", i18n_text(client->ui_lang,
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
    client_t **targets = NULL;
    int target_count = 0;

    if (count > 0) {
        targets = calloc((size_t)count, sizeof(*targets));
        if (!targets) {
            pthread_rwlock_unlock(&g_room->lock);
            return;
        }
    }

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
        targets[i]->unread_mentions++;
        client_queue_bell(targets[i]);
        client_release(targets[i]);
    }
    free(targets);
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

static int normal_visible_message_count(const client_t *client) {
    if (!client || !client->mute_joins) {
        return room_get_message_count(g_room);
    }

    message_t messages[MAX_MESSAGES];
    int message_count = room_copy_messages(g_room, 0, messages, MAX_MESSAGES);
    int count = 0;
    for (int i = 0; i < message_count; i++) {
        if (!system_message_is_join_leave(&messages[i])) {
            count++;
        }
    }
    return count;
}

static void normal_scroll_to_latest(client_t *client) {
    if (!client) return;
    history_view_scroll_to_latest(&client->scroll_pos, &client->follow_tail,
                                  normal_visible_message_count(client),
                                  history_view_height(client->height));
}

static void normal_scroll_by(client_t *client, int delta) {
    if (!client) return;
    history_view_scroll_by(&client->scroll_pos, &client->follow_tail,
                           normal_visible_message_count(client),
                           history_view_height(client->height), delta);
}

static void normal_enter_insert(client_t *client) {
    if (!client) return;
    client->mode = MODE_INSERT;
    client->follow_tail = true;
    client->unread_mentions = 0;
    tui_render_screen(client);
}

static void dismiss_command_output(client_t *client) {
    bool was_motd;

    if (!client) return;

    was_motd = client->show_motd;
    client->command_output[0] = '\0';
    client->command_output_scroll = 0;
    client->command_output_kind = TNT_COMMAND_OUTPUT_NONE;
    client->show_motd = false;
    if (was_motd) {
        client->mode = MODE_INSERT;
        client->follow_tail = true;
        client->unread_mentions = 0;
        normal_scroll_to_latest(client);
    } else {
        client->mode = MODE_NORMAL;
    }
    tui_render_screen(client);
}

typedef enum {
    PAGER_ACTION_NONE,
    PAGER_ACTION_SCROLL,
    PAGER_ACTION_CLOSE,
    PAGER_ACTION_REFRESH
} pager_action_t;

static int pager_page_height(client_t *client) {
    int page = client->height - 2;
    if (page < 1) page = 1;
    return page;
}

static void pager_scroll_by(int *scroll_pos, int delta) {
    *scroll_pos += delta;
    if (*scroll_pos < 0) {
        *scroll_pos = 0;
    }
}

static pager_action_t pager_apply_key(client_t *client, unsigned char key,
                                      int *scroll_pos, bool allow_refresh) {
    int page = pager_page_height(client);
    int half = page / 2;
    if (half < 1) half = 1;

    if (key == 'q') {
        return PAGER_ACTION_CLOSE;
    } else if (key == 'j') {
        pager_scroll_by(scroll_pos, 1);
        return PAGER_ACTION_SCROLL;
    } else if (key == 'k') {
        pager_scroll_by(scroll_pos, -1);
        return PAGER_ACTION_SCROLL;
    } else if (key == 4) {  /* Ctrl+D: half page down */
        pager_scroll_by(scroll_pos, half);
        return PAGER_ACTION_SCROLL;
    } else if (key == 21) {  /* Ctrl+U: half page up */
        pager_scroll_by(scroll_pos, -half);
        return PAGER_ACTION_SCROLL;
    } else if (key == 6 || key == ' ') {  /* Ctrl+F / Space: page down */
        pager_scroll_by(scroll_pos, page);
        return PAGER_ACTION_SCROLL;
    } else if (key == 2 || key == 'b') {  /* Ctrl+B / b: page up */
        pager_scroll_by(scroll_pos, -page);
        return PAGER_ACTION_SCROLL;
    } else if (key == 'g') {
        *scroll_pos = 0;
        return PAGER_ACTION_SCROLL;
    } else if (key == 'G') {
        *scroll_pos = 999;
        return PAGER_ACTION_SCROLL;
    } else if ((key == 'r' || key == 'R') && allow_refresh) {
        return PAGER_ACTION_REFRESH;
    } else if (key == 27) {
        char seq[3];
        int n = ssh_channel_read_timeout(client->channel, seq, 1, 0, 50);
        if (n != 1) {
            return PAGER_ACTION_CLOSE;
        }
        if (seq[0] != '[') {
            return PAGER_ACTION_NONE;
        }

        n = ssh_channel_read_timeout(client->channel, &seq[1], 1, 0, 50);
        if (n != 1) {
            return PAGER_ACTION_NONE;
        }

        if (seq[1] == 'A') {          /* Up arrow */
            pager_scroll_by(scroll_pos, -1);
            return PAGER_ACTION_SCROLL;
        } else if (seq[1] == 'B') {   /* Down arrow */
            pager_scroll_by(scroll_pos, 1);
            return PAGER_ACTION_SCROLL;
        } else if (seq[1] == 'H') {   /* Home */
            *scroll_pos = 0;
            return PAGER_ACTION_SCROLL;
        } else if (seq[1] == 'F') {   /* End */
            *scroll_pos = 999;
            return PAGER_ACTION_SCROLL;
        } else if (seq[1] >= '1' && seq[1] <= '6') {
            n = ssh_channel_read_timeout(client->channel, &seq[2], 1, 0, 50);
            if (n == 1 && seq[2] == '~') {
                if (seq[1] == '5') {        /* PageUp */
                    pager_scroll_by(scroll_pos, -page);
                    return PAGER_ACTION_SCROLL;
                } else if (seq[1] == '6') { /* PageDown */
                    pager_scroll_by(scroll_pos, page);
                    return PAGER_ACTION_SCROLL;
                } else if (seq[1] == '1') { /* Home */
                    *scroll_pos = 0;
                    return PAGER_ACTION_SCROLL;
                } else if (seq[1] == '4') { /* End */
                    *scroll_pos = 999;
                    return PAGER_ACTION_SCROLL;
                }
            }
        }
    }

    return PAGER_ACTION_NONE;
}

/* Join up to `shown` candidate strings into `out` as "a  b  c", adding a
 * "(+K)" suffix when more candidates exist than are shown. */
static void build_candidate_hint(char *out, size_t out_size,
                                 const char **cands, size_t n, size_t shown) {
    size_t pos = 0;
    out[0] = '\0';
    for (size_t i = 0; i < n && i < shown; i++) {
        buffer_appendf(out, out_size, &pos, "%s%s", i ? "  " : "", cands[i]);
    }
    if (n > shown) {
        buffer_appendf(out, out_size, &pos, "  (+%u)", (unsigned)(n - shown));
    }
}

/* Case-insensitive prefix filter over an ad-hoc candidate array, mirroring
 * command_catalog_complete() for argument completion.  Returns the total match
 * count; writes up to `maxm` matches into `matches` and the longest common
 * prefix of all matches into `lcp`. */
static size_t prefix_filter(const char **cands, size_t ncand,
                            const char *prefix, const char **matches,
                            size_t maxm, char *lcp, size_t lcp_size) {
    size_t count = 0;
    size_t plen = prefix ? strlen(prefix) : 0;

    if (lcp && lcp_size > 0) {
        lcp[0] = '\0';
    }
    for (size_t i = 0; i < ncand; i++) {
        const char *name = cands[i];
        if (!name) {
            continue;
        }
        if (plen > 0 && strncasecmp(name, prefix, plen) != 0) {
            continue;
        }
        if (matches && count < maxm) {
            matches[count] = name;
        }
        if (lcp && lcp_size > 0) {
            if (count == 0) {
                snprintf(lcp, lcp_size, "%s", name);
            } else {
                size_t k = 0;
                while (lcp[k] && name[k] && lcp[k] == name[k]) {
                    k++;
                }
                lcp[k] = '\0';
            }
        }
        count++;
    }
    return count;
}

/* Tab completion for COMMAND mode.  Completes the command name when no
 * argument has been started, otherwise completes the first argument for the
 * handful of commands with a closed/known candidate set (theme, lang, and
 * online usernames for msg).  Mutates client->command_input and renders. */
static void command_tab_complete(client_t *client) {
    char *buf = client->command_input;
    size_t cap = sizeof(client->command_input);
    char *sp = strchr(buf, ' ');

    if (sp == NULL) {
        /* Command name completion. */
        const char *out[16];
        char lcp[64];
        size_t n = command_catalog_complete(buf, out, 16, lcp, sizeof(lcp));
        if (n == 0) {
            client_send(client, "\a", 1);
            return;
        }
        if (n == 1) {
            snprintf(buf, cap, "%s ", out[0]);
            tui_render_command_input(client);
            return;
        }
        if (strlen(lcp) > strlen(buf)) {
            snprintf(buf, cap, "%s", lcp);
        }
        char hint[512];
        build_candidate_hint(hint, sizeof(hint), out, n, 12);
        tui_render_command_hint(client, hint);
        return;
    }

    /* Argument completion (first argument only). */
    char cmd[32];
    size_t cmdlen = (size_t)(sp - buf);
    if (cmdlen == 0 || cmdlen >= sizeof(cmd)) {
        return;
    }
    memcpy(cmd, buf, cmdlen);
    cmd[cmdlen] = '\0';

    const char *argregion = sp;
    while (*argregion == ' ') {
        argregion++;
    }
    if (strchr(argregion, ' ') != NULL) {
        return;  /* past the first argument: nothing to complete */
    }

    const char *cands[64];
    char namebufs[64][MAX_USERNAME_LEN];
    size_t ncand = 0;

    if (strcasecmp(cmd, "theme") == 0 || strcasecmp(cmd, "color") == 0) {
        size_t tc = theme_count();
        for (size_t i = 0; i < tc && ncand < 64; i++) {
            cands[ncand++] = theme_at(i)->name;
        }
    } else if (strcasecmp(cmd, "lang") == 0 ||
               strcasecmp(cmd, "language") == 0) {
        cands[ncand++] = "en";
        cands[ncand++] = "zh";
    } else if (strcasecmp(cmd, "msg") == 0 || strcasecmp(cmd, "w") == 0) {
        pthread_rwlock_rdlock(&g_room->lock);
        for (int i = 0; i < g_room->client_count && ncand < 64; i++) {
            snprintf(namebufs[ncand], MAX_USERNAME_LEN, "%s",
                     g_room->clients[i]->username);
            cands[ncand] = namebufs[ncand];
            ncand++;
        }
        pthread_rwlock_unlock(&g_room->lock);
    } else {
        return;
    }

    const char *out[64];
    char lcp[MAX_USERNAME_LEN];
    size_t n = prefix_filter(cands, ncand, argregion, out, 64, lcp,
                             sizeof(lcp));
    if (n == 0) {
        client_send(client, "\a", 1);
        return;
    }
    if (n == 1) {
        snprintf(buf, cap, "%s %s ", cmd, out[0]);
        tui_render_command_input(client);
        return;
    }
    if (strlen(lcp) > strlen(argregion)) {
        snprintf(buf, cap, "%s %s", cmd, lcp);
    }
    char hint[512];
    build_candidate_hint(hint, sizeof(hint), out, n, 12);
    tui_render_command_hint(client, hint);
}

static void input_replace(char *input, size_t input_size, size_t *input_len,
                          const char *value) {
    size_t len = strnlen(value, input_size - 1);

    memcpy(input, value, len);
    input[len] = '\0';
    *input_len = len;
}

/* Reads the remainder of an "ESC [ <n> ~" sequence and applies it.  Returns
 * true in every case: an editing key that this build does not implement is
 * still consumed, because letting it fall through would mean leaving INSERT
 * mode and discarding the message the user is composing. */
static bool handle_insert_csi_tilde(client_t *client, editor_t *ed,
                                    char first) {
    char terminator;
    int n = ssh_channel_read_timeout(client->channel, &terminator, 1, 0, 50);

    if (n != 1 || terminator != '~') {
        return true;  /* Not a form we know; consume it either way. */
    }

    switch (first) {
    case '1':
    case '7':
        editor_move_home(ed);
        break;
    case '3':
        editor_delete_next_cluster(ed);
        break;
    case '4':
    case '8':
        editor_move_end(ed);
        break;
    default:
        return true;
    }

    tui_render_input(client, ed);
    return true;
}

/* Handle a single key press.  Returns true if the key was fully consumed
 * (no further character buffering needed). */
static bool handle_key(client_t *client, unsigned char key, editor_t *ed,
                       size_t *command_input_len) {
    /* Handle Ctrl+C (Exit or switch to NORMAL) */
    if (key == 3) {
        client_mode_t previous_mode = client->mode;
        if (client->show_help) {
            client->show_help = false;
            tui_render_screen(client);
            return true;
        }
        if (client->command_output[0] != '\0') {
            dismiss_command_output(client);
            return true;
        }
        if (!tnt_keymap_uses_modes(client->keymap)) {
            /* No mode to fall back to.  Clear what is being composed, and
             * when there is nothing to clear, say how to leave rather than
             * disconnecting someone who pressed a habitual Ctrl+C. */
            if (editor_len(ed) > 0) {
                editor_clear(ed);
                tui_render_input(client, ed);
            } else {
                client_printf(client, "\a");
                tui_render_command_hint(
                    client, i18n_text(client->ui_lang, I18N_HINT_QUIT));
            }
            return true;
        }
        if (previous_mode != MODE_NORMAL) {
            client->mode = MODE_NORMAL;
            client->command_input[0] = '\0';
            *command_input_len = 0;
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
        pager_action_t action;

        if (key == 'l' || key == 'L') {
            client->ui_lang = i18n_next_ui_lang(client->ui_lang);
            client->help_scroll_pos = 0;
            tui_render_help(client);
            return true;
        }

        action = pager_apply_key(client, key, &client->help_scroll_pos, false);
        if (action == PAGER_ACTION_CLOSE) {
            client->show_help = false;
            tui_render_screen(client);
        } else if (action == PAGER_ACTION_SCROLL) {
            tui_render_help(client);
        }
        return true;  /* Key consumed */
    }

    /* Handle command output / MOTD display.  MOTD remains a simple notice;
     * command output behaves like a small pager so long results can be read. */
    if (client->command_output[0] != '\0') {
        pager_action_t action;

        if (client->show_motd) {
            dismiss_command_output(client);
            return true;
        }

        action = pager_apply_key(client, key, &client->command_output_scroll,
                                 true);
        if (action == PAGER_ACTION_CLOSE) {
            dismiss_command_output(client);
        } else if (action == PAGER_ACTION_SCROLL) {
            tui_render_command_output(client);
        } else if (action == PAGER_ACTION_REFRESH) {
            if (commands_refresh_active_output(client)) {
                tui_render_command_output(client);
            }
        }
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
                        if (seq[1] == 'C') {  /* Right */
                            editor_move_right(ed);
                            tui_render_input(client, ed);
                            return true;
                        } else if (seq[1] == 'D') {  /* Left */
                            editor_move_left(ed);
                            tui_render_input(client, ed);
                            return true;
                        } else if (seq[1] == 'H') {  /* Home */
                            editor_move_home(ed);
                            tui_render_input(client, ed);
                            return true;
                        } else if (seq[1] == 'F') {  /* End */
                            editor_move_end(ed);
                            tui_render_input(client, ed);
                            return true;
                        } else if (seq[1] == '1' || seq[1] == '3' ||
                                   seq[1] == '4' || seq[1] == '7' ||
                                   seq[1] == '8') {
                            /* "ESC [ <n> ~": Home, Delete, and End. */
                            return handle_insert_csi_tilde(client, ed, seq[1]);
                        } else if (seq[1] == '5' || seq[1] == '6') {
                            /* PgUp / PgDn.  In the default keymap the history
                             * has to be readable without leaving the input,
                             * because there is no mode to leave it for. */
                            char tilde;
                            int t = ssh_channel_read_timeout(client->channel,
                                                             &tilde, 1, 0, 50);
                            if (t == 1 && tilde == '~' &&
                                !tnt_keymap_uses_modes(client->keymap)) {
                                int page = history_view_height(client->height);
                                normal_scroll_by(client,
                                                 seq[1] == '5' ? -page : page);
                                tui_render_screen(client);
                                tui_render_input(client, ed);
                            }
                            return true;
                        } else if (seq[1] == 'A') {  /* Up — walk back through sent history */
                            /* With a caret, Up must not silently replace text
                             * the user is still composing. */
                            if (!tnt_keymap_uses_modes(client->keymap) &&
                                editor_len(ed) > 0) {
                                return true;
                            }
                            if (client->insert_history_count > 0 &&
                                client->insert_history_pos > 0) {
                                client->insert_history_pos--;
                                editor_set_text(
                                    ed,
                                    client->insert_history[
                                        client->insert_history_pos]);
                                tui_render_input(client, ed);
                            }
                            return true;
                        } else if (seq[1] == 'B') {  /* Down — walk forward */
                            if (client->insert_history_pos <
                                client->insert_history_count - 1) {
                                client->insert_history_pos++;
                                editor_set_text(
                                    ed,
                                    client->insert_history[
                                        client->insert_history_pos]);
                            } else {
                                client->insert_history_pos =
                                    client->insert_history_count;
                                editor_clear(ed);
                            }
                            tui_render_input(client, ed);
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
                                bool invalid_utf8 = false;
                                tnt_input_utf8_state_t paste_utf8 = {0};
                                /* Collect the paste separately, then insert
                                 * it at the caret in one step. */
                                char pasted[MAX_MESSAGE_LEN] = {0};
                                size_t pasted_len = 0;
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
                                            int status =
                                                tnt_input_append_stream_byte(
                                                    pasted, sizeof(pasted),
                                                    &pasted_len,
                                                    &paste_utf8,
                                                    (unsigned char)tail[i],
                                                    true);
                                            if (status &
                                                TNT_INPUT_APPEND_OVERFLOW) {
                                                overflow = true;
                                            }
                                            if (status &
                                                TNT_INPUT_APPEND_INVALID_UTF8) {
                                                invalid_utf8 = true;
                                            }
                                        }
                                        continue;
                                    }
                                    int status = tnt_input_append_stream_byte(
                                        pasted, sizeof(pasted), &pasted_len,
                                        &paste_utf8, (unsigned char)b, true);
                                    if (status & TNT_INPUT_APPEND_OVERFLOW) {
                                        overflow = true;
                                    }
                                    if (status & TNT_INPUT_APPEND_INVALID_UTF8) {
                                        invalid_utf8 = true;
                                    }
                                }
                                if (tnt_input_utf8_state_finish(
                                        &paste_utf8) &
                                    TNT_INPUT_APPEND_INVALID_UTF8) {
                                    invalid_utf8 = true;
                                }
                                if (pasted_len > 0 &&
                                    !editor_insert_bytes(ed, pasted,
                                                         pasted_len)) {
                                    overflow = true;
                                }
                                tui_render_input(client, ed);
                                if (overflow || invalid_utf8) {
                                    client_send(client, "\a", 1);
                                }
                            }
                            return true;
                        }
                        /* A CSI sequence this build does not implement.
                         * Swallow it.  Letting it reach the plain-ESC branch
                         * below is what used to drop the user into NORMAL
                         * mode mid-word and discard the message. */
                        return true;
                    }
                }
                if (!tnt_keymap_uses_modes(client->keymap)) {
                    /* Nothing to escape to.  Dismissing a panel is the only
                     * thing Esc does here; it never takes the keyboard away
                     * from the person typing. */
                    if (client->show_help) {
                        client->show_help = false;
                        tui_render_screen(client);
                        tui_render_input(client, ed);
                    }
                    return true;
                }
                /* Plain ESC — fall through to NORMAL mode */
                client->mode = MODE_NORMAL;
                normal_scroll_to_latest(client);
                tui_render_screen(client);
                return true;
            } else if (key == '\r' || key == '\n') {  /* Enter */
                const char *input = editor_text(ed);
                if (!tnt_keymap_uses_modes(client->keymap) &&
                    input[0] == '/') {
                    tnt_command_id_t id;

                    if (input[1] == '/') {
                        /* "//text" is how a message that really starts with a
                         * slash gets sent.  Drop one slash and fall through. */
                        char literal[MAX_MESSAGE_LEN];
                        snprintf(literal, sizeof(literal), "%s", input + 1);
                        editor_set_text(ed, literal);
                        input = editor_text(ed);
                    } else if (command_catalog_match(input + 1, &id, NULL)) {
                        /* A recognised command runs; anything else — a path
                         * like /usr/local/bin, a typo — is just a message. */
                        snprintf(client->command_input,
                                 sizeof(client->command_input), "%s",
                                 input + 1);
                        *command_input_len = strnlen(
                            client->command_input,
                            sizeof(client->command_input) - 1);
                        editor_clear(ed);
                        commands_dispatch(client);
                        client->command_input[0] = '\0';
                        *command_input_len = 0;
                        client->redraw_pending = true;
                        return true;
                    }
                }
                if (input[0] != '\0') {
                    bool is_action = editor_len(ed) > 4 &&
                                     strncmp(input, "/me ", 4) == 0;
                    size_t action_len = is_action ? editor_len(ed) - 4 : 0;
                    size_t action_username_len = 0;

                    if (is_action) {
                        action_username_len = strlen(client->username);
                    }
                    if (is_action &&
                        action_username_len + 1 + action_len >=
                            MAX_MESSAGE_LEN) {
                        client_send(client, "\a", 1);
                        return true;
                    }

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
                    if (is_action) {
                        msg.username[0] = '*';
                        msg.username[1] = '\0';
                        memcpy(msg.content, client->username,
                               action_username_len);
                        msg.content[action_username_len] = ' ';
                        memcpy(msg.content + action_username_len + 1,
                               input + 4, action_len + 1);
                    } else {
                        snprintf(msg.username, sizeof(msg.username), "%s", client->username);
                        snprintf(msg.content, sizeof(msg.content), "%s", input);
                    }
                    if (message_save(&msg) == 0) {
                        room_broadcast(g_room, &msg);
                        notify_mentions(msg.content, client);
                        tnt_module_runtime_publish_message_created(&msg);
                    } else {
                        fprintf(stderr, "interactive: failed to persist message\n");
                    }
                    editor_clear(ed);
                }
                /* The room update/redraw path at the top of the session loop
                 * owns the post-send repaint.  Deferring it lets a buffered
                 * burst of complete messages collapse into one screen render
                 * without changing persistence or broadcast ordering. */
                client->redraw_pending = true;
                return true;  /* Key consumed */
            } else if (key == 127 || key == 8) {  /* Backspace */
                if (editor_delete_prev_cluster(ed)) {
                    tui_render_input(client, ed);
                }
                return true;  /* Key consumed */
            } else if (key == 23) { /* Ctrl+W (Delete Word) */
                if (editor_delete_prev_word(ed)) {
                    tui_render_input(client, ed);
                }
                return true;
            } else if (key == 21) { /* Ctrl+U (Delete Line) */
                if (editor_len(ed) > 0) {
                    editor_clear(ed);
                    tui_render_input(client, ed);
                }
                return true;
            } else if (key == 9) { /* Tab: complete @mention */
                /* Walk back from end to find the start of the trailing
                 * "@…" token (an '@' not preceded by an alphanumeric).
                 * If found, scan g_room for the first case-insensitive
                 * username prefix-match (cycling past self) and replace
                 * the token. */
                char input[MAX_MESSAGE_LEN];
                snprintf(input, sizeof(input), "%s", editor_text(ed));
                size_t in_len = editor_len(ed);
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
                    size_t plen = in_len - (size_t)at_idx - 1;
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
                            size_t pos = (size_t)at_idx + 1;
                            memcpy(input + pos, match, mlen);
                            pos += mlen;
                            input[pos++] = ' ';
                            input[pos] = '\0';
                            editor_set_text(ed, input);
                            tui_render_input(client, ed);
                        }
                    }
                }
                return true;
            }
            break;

        case MODE_NORMAL: {
            int nm_msg_height = history_view_height(client->height);

            if (key == 'i' || key == 'a' || key == 'A' ||
                key == 'o' || key == 'O') {
                normal_enter_insert(client);
                return true;
            } else if (key == ':') {
                client->mode = MODE_COMMAND;
                client->command_input[0] = '\0';
                *command_input_len = 0;
                tui_render_command_input(client);
                return true;
            } else if (key == '/') {
                client->mode = MODE_COMMAND;
                snprintf(client->command_input, sizeof(client->command_input),
                         "search ");
                *command_input_len = sizeof("search ") - 1;
                tui_render_command_input(client);
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
                                input_replace(
                                    client->command_input,
                                    sizeof(client->command_input),
                                    command_input_len,
                                    client->command_history[
                                        client->command_history_pos]);
                                tui_render_command_input(client);
                            }
                            return true;
                        } else if (seq[1] == 'B') {  /* Down arrow */
                            if (client->command_history_pos < client->command_history_count - 1) {
                                client->command_history_pos++;
                                input_replace(
                                    client->command_input,
                                    sizeof(client->command_input),
                                    command_input_len,
                                    client->command_history[
                                        client->command_history_pos]);
                            } else {
                                client->command_history_pos = client->command_history_count;
                                client->command_input[0] = '\0';
                                *command_input_len = 0;
                            }
                            tui_render_command_input(client);
                            return true;
                        }
                    }
                }
                client->mode = MODE_NORMAL;
                client->command_input[0] = '\0';
                *command_input_len = 0;
                tui_render_screen(client);
                return true;
            } else if (key == '\r' || key == '\n') {
                commands_dispatch(client);
                *command_input_len = 0;
                return true;  /* Key consumed */
            } else if (key == 127 || key == 8) {  /* Backspace */
                if (client->command_input[0] != '\0') {
                    *command_input_len = utf8_remove_last_char(
                        client->command_input, *command_input_len);
                    tui_render_command_input(client);
                }
                return true;  /* Key consumed */
            } else if (key == 23) { /* Ctrl+W (Delete Word) */
                if (client->command_input[0] != '\0') {
                    *command_input_len = utf8_remove_last_word(
                        client->command_input, *command_input_len);
                    tui_render_command_input(client);
                }
                return true;
            } else if (key == 21) { /* Ctrl+U (Delete Line) */
                if (client->command_input[0] != '\0') {
                    client->command_input[0] = '\0';
                    *command_input_len = 0;
                    tui_render_command_input(client);
                }
                return true;
            } else if (key == 9) { /* Tab: complete command name or argument */
                command_tab_complete(client);
                *command_input_len = strnlen(
                    client->command_input,
                    sizeof(client->command_input) - 1);
                return true;
            }
            break;

        default:
            break;
    }

    return false;  /* Key not consumed */
}

static int64_t input_monotonic_millis(void) {
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
        return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    }
    return (int64_t)time(NULL) * 1000;
}

/* Complete a channel before releasing its SSH transport.  Flushing the
 * exit-status/CLOSE packets only proves they reached the kernel; immediately
 * freeing the session can still race OpenSSH before it consumes them and turn
 * a valid remote status into ssh(1)'s transport-error status 255.  Wait for
 * the peer CHANNEL_CLOSE callback with a fixed upper bound so a broken client
 * cannot strand its detached session worker or graceful server shutdown. */
static void input_finish_channel(client_t *client, int exit_status) {
    ssh_event event = NULL;
    bool event_added = false;

    if (!client || !client->channel || !client->session) {
        return;
    }

    (void)ssh_channel_request_send_exit_status(client->channel, exit_status);
    (void)ssh_blocking_flush(client->session, 1000);
    (void)ssh_channel_send_eof(client->channel);
    (void)ssh_blocking_flush(client->session, 1000);
    (void)ssh_channel_close(client->channel);
    (void)ssh_blocking_flush(client->session, 1000);

    if (atomic_load(&client->channel_closed) ||
        !ssh_is_connected(client->session)) {
        return;
    }

    event = ssh_event_new();
    if (event && ssh_event_add_session(event, client->session) == SSH_OK) {
        int64_t deadline = input_monotonic_millis() +
                           CHANNEL_CLOSE_ACK_TIMEOUT_MS;
        event_added = true;

        while (!atomic_load(&client->channel_closed)) {
            int64_t remaining = deadline - input_monotonic_millis();
            if (remaining <= 0) {
                break;
            }
            if (ssh_event_dopoll(event, (int)remaining) == SSH_ERROR) {
                break;
            }
        }
    }

    if (event_added) {
        (void)ssh_event_remove_session(event, client->session);
    }
    if (event) {
        ssh_event_free(event);
    }
}

static int input_poll_timeout_ms(int64_t now_ms, int64_t last_keepalive_ms,
                                 int64_t last_activity_ms,
                                 bool idle_timeout_enabled) {
    int64_t deadline = last_keepalive_ms + KEEPALIVE_INTERVAL_MS;

    if (idle_timeout_enabled) {
        int64_t idle_deadline = last_activity_ms +
                                (int64_t)g_idle_timeout * 1000;
        if (idle_deadline < deadline) {
            deadline = idle_deadline;
        }
    }

    if (deadline <= now_ms) return 0;
    if (deadline - now_ms > INT_MAX) return INT_MAX;
    return (int)(deadline - now_ms);
}

/* Drain as much of the bounded outbox as the current SSH window permits.
 * A render can exceed one fairness-budget chunk; stopping only when no
 * progress is possible avoids both stranded output and a POLLOUT busy loop. */
static int input_flush_client_output(client_t *client) {
    size_t before;
    size_t after;

    do {
        before = client_pending_output(client);
        if (client_flush_output(client) != 0) {
            return -1;
        }
        after = client_pending_output(client);
    } while (after > 0 && after < before);

    return 0;
}

void input_run_session(client_t *client) {
    editor_t ed;
    editor_reset(&ed);
    size_t command_input_len = 0;
    char buf[4];
    bool joined_room = false;
    bool bracketed_paste_enabled = false;
    uint64_t seen_update_seq;
    int64_t last_keepalive_ms = input_monotonic_millis();
    int64_t last_activity_ms = last_keepalive_ms;
    int64_t last_room_render_ms = 0;

    /* Terminal size already set from PTY request */
    client->mode = MODE_INSERT;
    /* The login name selects neither identity nor nickname, which is what
     * leaves `ssh vim@host` free to mean "give me the vim keys". */
    client->keymap = tnt_keymap_from_login(client->ssh_login, g_default_keymap);
    client->follow_tail = true;
    client->ui_lang = g_default_ui_lang;
    client->connected = true;
    client->command_history_count = 0;
    client->command_history_pos = 0;
    client->command_input[0] = '\0';
    client->command_output_scroll = 0;
    client->command_output_kind = TNT_COMMAND_OUTPUT_NONE;
    client->connect_time = time(NULL);
    client->last_active = time(NULL);

    /* Check for exec command */
    if (client->exec_command[0] != '\0' || client->exec_command_too_long) {
        int exit_status = exec_dispatch(client);
        input_finish_channel(client, exit_status);
        goto cleanup;
    }

    /* Read username */
    if (read_username(client) < 0) {
        goto cleanup;
    }

    /* Add to room */
    int join_rc = room_add_client(g_room, client);
    if (join_rc < 0) {
        if (join_rc == -2) {
            client_printf(client,
                          i18n_text(client->ui_lang,
                                    I18N_NICK_TAKEN_FORMAT),
                          client->username);
        } else {
            client_printf(client, "%s", i18n_text(client->ui_lang,
                                                  I18N_ROOM_FULL));
        }
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
    system_message_make_join(&join_msg, client->username, client->ui_lang);
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
                    client->command_output_scroll = 0;
                    client->command_output_kind = TNT_COMMAND_OUTPUT_NONE;
                    client->show_motd = true;
                    seen_update_seq = room_get_update_seq(g_room);
                    tui_render_motd(client);
                    goto main_loop;
                }
            }
        }
    }

    /* Render initial screen */
    seen_update_seq = room_get_update_seq(g_room);
    tui_render_screen(client);
    last_room_render_ms = input_monotonic_millis();

main_loop:

    /* Main input loop */
    while (client->connected && ssh_channel_is_open(client->channel)) {
        bool room_updated = false;
        bool room_wake_cleared = false;
        /* Prefer already-buffered channel input over repainting an obsolete
         * intermediate screen.  Human keypresses still repaint immediately,
         * while pasted/batched messages collapse UI work until the receive
         * buffer is empty. */
        int ready = ssh_channel_poll_timeout(client->channel, 0, 0);
        if (ready < 0) {
            break;
        }
        bool input_buffered = ready > 0;
        uint64_t current_update_seq = room_get_update_seq(g_room);
        int64_t loop_now_ms = input_monotonic_millis();

        if (client_flush_pending_bells(client) != 0) {
            break;
        }

        if (current_update_seq != seen_update_seq) {
            room_updated = true;
        }

        if (client->command_output_kind == TNT_COMMAND_OUTPUT_INBOX &&
            client->command_output[0] != '\0' &&
            client->unread_whispers > 0) {
            commands_refresh_active_output(client);
            client->redraw_pending = true;
        }

        bool redraw_requested = false;
        if (!input_buffered) {
            redraw_requested = atomic_exchange(&client->redraw_pending,
                                                false);
        }
        bool room_view_visible = !client->show_help && !client->show_motd &&
                                 client->command_output[0] == '\0';
        bool room_render_due = room_updated && room_view_visible &&
            (last_room_render_ms == 0 ||
             loop_now_ms - last_room_render_ms >=
                 ROOM_REDRAW_MIN_INTERVAL_MS);

        if (!input_buffered && room_updated && !room_view_visible) {
            /* Help/MOTD/command output owns the screen.  Remember the room
             * generation without waking this session for every hidden
             * intermediate update; closing the overlay renders a fresh room
             * snapshot. */
            seen_update_seq = current_update_seq;
            atomic_store(&client->wake_pending, false);
            room_wake_cleared = true;
        }

        if (!input_buffered && (redraw_requested || room_render_due)) {
            if (room_updated && room_view_visible) {
                /* Mark only the generation sampled before rendering.  A
                 * broadcast racing the snapshot remains visible to the next
                 * generation check instead of being marked as already seen. */
                seen_update_seq = current_update_seq;
            }
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
                last_room_render_ms = input_monotonic_millis();
                if (client->mode == MODE_INSERT && editor_len(&ed) > 0) {
                    tui_render_input(client, &ed);
                }
            }
            if (room_updated) {
                atomic_store(&client->wake_pending, false);
                room_wake_cleared = true;
            }
        }

        if (input_flush_client_output(client) != 0) {
            break;
        }

        int64_t now_ms = input_monotonic_millis();
        if (g_idle_timeout > 0 && joined_room &&
            now_ms - last_activity_ms >= (int64_t)g_idle_timeout * 1000) {
            client_printf(client,
                          i18n_text(client->ui_lang,
                                    I18N_IDLE_TIMEOUT_FORMAT),
                          g_idle_timeout / 60);
            break;
        }

        if (now_ms - last_keepalive_ms >= KEEPALIVE_INTERVAL_MS) {
            if (ssh_send_keepalive(client->session) != SSH_OK) {
                break;
            }
            last_keepalive_ms = now_ms;
        }

        /* First consume bytes libssh already buffered.  Waiting on the raw
         * socket before this check can sleep forever after libssh read several
         * channel bytes from one packet and the kernel fd became empty. */
        if (ready == 0) {
            if (room_wake_cleared &&
                room_get_update_seq(g_room) != seen_update_seq) {
                /* A broadcaster can observe wake_pending=true while this
                 * thread is rendering and intentionally skip its signal.
                 * Clear, recheck the generation, and loop before blocking to
                 * close that coalescing race. */
                continue;
            }
            int session_fd = ssh_get_fd(client->session);
            if (session_fd < 0) {
                break;
            }
            int ssh_poll_flags = ssh_get_poll_flags(client->session);
            sigset_t wait_mask;
            int timeout_ms = input_poll_timeout_ms(
                now_ms, last_keepalive_ms, last_activity_ms,
                g_idle_timeout > 0 && joined_room);
            bool defer_room_wake = false;
            if (room_updated && room_view_visible && !room_render_due) {
                int64_t room_wait_ms = last_room_render_ms +
                    ROOM_REDRAW_MIN_INTERVAL_MS - now_ms;
                if (room_wait_ms < 0) {
                    room_wait_ms = 0;
                }
                if (room_wait_ms < timeout_ms) {
                    timeout_ms = (int)room_wait_ms;
                }
                /* Keep the level bit set while updates are coalescing.  New
                 * broadcasts then avoid another signal/context switch; the
                 * bounded timeout performs the single consolidated redraw. */
                defer_room_wake = true;
            }
            struct timespec wait_timeout = {
                .tv_sec = timeout_ms / 1000,
                .tv_nsec = (long)(timeout_ms % 1000) * 1000000L,
            };
#if defined(__APPLE__)
            fd_set read_fds;
            fd_set write_fds;
            fd_set error_fds;
#endif
            struct pollfd wait_fd = {
                .fd = session_fd,
                .events = POLLIN |
                          ((ssh_poll_flags & SSH_WRITE_PENDING)
                               ? POLLOUT : 0),
                .revents = 0,
            };
            if (pthread_sigmask(SIG_SETMASK, NULL, &wait_mask) != 0) {
                break;
            }
            sigdelset(&wait_mask, SIGUSR1);

            int poll_rc;
            if (!defer_room_wake &&
                atomic_exchange(&client->wake_pending, false)) {
                poll_rc = 0;
            } else {
#if defined(__APPLE__)
                if (session_fd < FD_SETSIZE) {
                    FD_ZERO(&read_fds);
                    FD_ZERO(&write_fds);
                    FD_ZERO(&error_fds);
                    FD_SET(session_fd, &read_fds);
                    FD_SET(session_fd, &error_fds);
                    if (ssh_poll_flags & SSH_WRITE_PENDING) {
                        FD_SET(session_fd, &write_fds);
                    }
                    poll_rc = pselect(session_fd + 1, &read_fds,
                                      &write_fds, &error_fds,
                                      &wait_timeout, &wait_mask);
                } else {
                    /* Darwin has no ppoll().  pselect() is atomic with the
                     * wake-signal mask, but fd_set cannot represent a high
                     * descriptor.  Keep SIGUSR1 blocked and bound poll() to
                     * a short interval; wake_pending remains the level state
                     * that prevents a notification from being lost. */
                    int fallback_timeout = timeout_ms;
                    if (fallback_timeout > DARWIN_HIGH_FD_POLL_MS) {
                        fallback_timeout = DARWIN_HIGH_FD_POLL_MS;
                    }
                    poll_rc = poll(&wait_fd, 1, fallback_timeout);
                }
#else
                poll_rc = ppoll(&wait_fd, 1, &wait_timeout, &wait_mask);
#endif
            }

            if (poll_rc < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (poll_rc == 0) {
                continue;
            }

#if defined(__APPLE__)
            bool session_event;
            if (session_fd < FD_SETSIZE) {
                session_event = FD_ISSET(session_fd, &read_fds) ||
                                FD_ISSET(session_fd, &write_fds) ||
                                FD_ISSET(session_fd, &error_fds);
            } else {
                if (wait_fd.revents & POLLNVAL) {
                    break;
                }
                session_event =
                    wait_fd.revents & (POLLIN | POLLOUT | POLLERR | POLLHUP);
            }
#else
            if (wait_fd.revents & POLLNVAL) {
                break;
            }
            bool session_event =
                wait_fd.revents & (POLLIN | POLLOUT | POLLERR | POLLHUP);
#endif
            if (session_event) {
                /* Let libssh parse packets and run channel callbacks; channel
                 * data, if any, is then read below. */
                ready = ssh_channel_poll_timeout(client->channel, 0, 0);
                if (ready < 0) {
                    break;
                }
                if (ready == 0 &&
                    (!ssh_is_connected(client->session) ||
                     !ssh_channel_is_open(client->channel))) {
                    break;
                }
            }

            if (ready == 0) {
                continue;
            }
        }

        int n = ssh_channel_read(client->channel, buf, 1, 0);

        if (n <= 0) {
            /* EOF or error */
            break;
        }

        last_keepalive_ms = input_monotonic_millis();
        last_activity_ms = last_keepalive_ms;
        client->last_active = time(NULL);

        unsigned char b = buf[0];

        /* Handle special keys - returns true if key was consumed */
        bool key_consumed = handle_key(client, b, &ed,
                                       &command_input_len);

        /* Only add character to input if not consumed by handle_key */
        if (!key_consumed) {
            /* Add character to input (INSERT mode only) */
            if (client->mode == MODE_INSERT && !client->show_help &&
                client->command_output[0] == '\0') {
                if (b >= 32 && b < 127) {  /* ASCII printable */
                    char ch = (char)b;
                    if (editor_insert_bytes(&ed, &ch, 1)) {
                        if (ready <= n) {
                            tui_render_input(client, &ed);
                        }
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
                    if (!utf8_is_control_sequence(buf, char_len) &&
                        editor_insert_bytes(&ed, buf, (size_t)char_len)) {
                        if (ready <= char_len) {
                            tui_render_input(client, &ed);
                        }
                    } else {
                        client_send(client, "\a", 1);
                    }
                }
            } else if (client->mode == MODE_COMMAND && !client->show_help &&
                       client->command_output[0] == '\0') {
                if (b >= 32 && b < 127) {  /* ASCII printable */
                    int status = tnt_input_append_ascii(
                        client->command_input, sizeof(client->command_input),
                        &command_input_len, b);
                    if (status == TNT_INPUT_APPEND_OK) {
                        if (ready <= n) {
                            tui_render_command_input(client);
                        }
                    } else {
                        client_send(client, "\a", 1);
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
                    int status = tnt_input_append_utf8_sequence(
                        client->command_input, sizeof(client->command_input),
                        &command_input_len, buf, char_len);
                    if (status == TNT_INPUT_APPEND_OK) {
                        if (ready <= char_len) {
                            tui_render_command_input(client);
                        }
                    } else {
                        client_send(client, "\a", 1);
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
                                  client->ui_lang);

        client->connected = false;
        room_remove_client(g_room, client);
        room_broadcast(g_room, &leave_msg);
        message_save(&leave_msg);
    }

    /* Interactive shells otherwise look like a transport reset to OpenSSH
     * when the owning worker releases libssh state.  Send an explicit clean
     * channel result for :quit, Ctrl-C, stdin EOF, and other normal exits.
     * Shutdown-swept sockets simply reject these best-effort writes. */
    if (client->channel && ssh_channel_is_open(client->channel)) {
        input_finish_channel(client, 0);
    }

    ratelimit_release_ip(client->client_ip);

    client_release_session(client);

    /* Decrement connection count */
    ratelimit_decrement_total();
}
