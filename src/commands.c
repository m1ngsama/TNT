#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE  /* for strcasestr() on glibc */
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE /* for strcasestr() on macOS */
#endif
#include "commands.h"
#include "chat_room.h"
#include "client.h"
#include "command_catalog.h"
#include "common.h"
#include "i18n.h"
#include "manual.h"
#include "message.h"
#include "system_message.h"
#include "tui.h"
#include "utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Append `text` to the output buffer with every case-insensitive match of
 * `needle` wrapped in a reverse-yellow ANSI chip.  Preserves the original
 * casing of the matched substring.  needle == NULL or empty appends raw. */
static void append_highlighted(char *output, size_t buf_size, size_t *pos,
                               const char *text, const char *needle) {
    if (!needle || !*needle) {
        buffer_appendf(output, buf_size, pos, "%s", text);
        return;
    }
    size_t nlen = strlen(needle);
    const char *p = text;
    while (*p) {
        const char *hit = strcasestr(p, needle);
        if (!hit) {
            buffer_appendf(output, buf_size, pos, "%s", p);
            return;
        }
        if (hit > p) {
            buffer_append_bytes(output, buf_size, pos, p, (size_t)(hit - p));
        }
        buffer_append_bytes(output, buf_size, pos, "\033[7;33m", 7);
        buffer_append_bytes(output, buf_size, pos, hit, nlen);
        buffer_append_bytes(output, buf_size, pos, "\033[0m", 4);
        p = hit + nlen;
    }
}

void commands_dispatch(client_t *client) {
    char cmd_buf[256];
    strncpy(cmd_buf, client->command_input, sizeof(cmd_buf) - 1);
    cmd_buf[sizeof(cmd_buf) - 1] = '\0';
    char *cmd = cmd_buf;
    char output[MAX_COMMAND_OUTPUT_LEN] = {0};
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

    /* Save to command history */
    if (cmd[0] != '\0') {
        int max_hist = 16;
        if (client->command_history_count >= max_hist) {
            memmove(&client->command_history[0], &client->command_history[1],
                    (max_hist - 1) * sizeof(client->command_history[0]));
            client->command_history_count = max_hist - 1;
        }
        snprintf(client->command_history[client->command_history_count],
                 sizeof(client->command_history[0]), "%s", cmd);
        client->command_history_count++;
        client->command_history_pos = client->command_history_count;
    }

    if (cmd[0] == '\0') {
        /* Empty command */
        client->mode = MODE_NORMAL;
        client->command_input[0] = '\0';
        tui_render_screen(client);
        return;
    }

    tnt_command_id_t command_id;
    const char *arg = "";
    if (!command_catalog_match(cmd, &command_id, &arg)) {
        const char *suggestion = command_catalog_suggest(cmd);
        buffer_appendf(output, sizeof(output), &pos,
                       i18n_text(client->ui_lang,
                                 I18N_UNKNOWN_COMMAND_FORMAT),
                       cmd);
        if (suggestion) {
            buffer_appendf(output, sizeof(output), &pos,
                           i18n_text(client->ui_lang,
                                     I18N_DID_YOU_MEAN_FORMAT),
                           suggestion);
        }
        buffer_appendf(output, sizeof(output), &pos, "%s",
                       i18n_text(client->ui_lang, I18N_UNKNOWN_GUIDANCE));
        goto cmd_done;
    }

    if (command_id == TNT_COMMAND_USERS) {
        pthread_rwlock_rdlock(&g_room->lock);
        int total = g_room->client_count;
        buffer_appendf(output, sizeof(output), &pos,
                       "\033[1;36m%s\033[0m  \033[2;37m· %d\033[0m\n",
                       i18n_text(client->ui_lang, I18N_USERS_TITLE), total);

        time_t now = time(NULL);
        for (int i = 0; i < total; i++) {
            bool is_self = (g_room->clients[i] == client);
            int dur = (int)(now - g_room->clients[i]->connect_time);
            char dur_str[32];
            if (dur < 60) {
                snprintf(dur_str, sizeof(dur_str), "%ds", dur);
            } else if (dur < 3600) {
                snprintf(dur_str, sizeof(dur_str), "%dm", dur / 60);
            } else {
                snprintf(dur_str, sizeof(dur_str), "%dh%dm",
                         dur / 3600, (dur % 3600) / 60);
            }
            /* 1-column gutter: ▎ for you, blank for others */
            buffer_appendf(output, sizeof(output), &pos,
                           "%s  \033[37m%s\033[0m  \033[2;37m· %s\033[0m\n",
                           is_self ? "\033[36m▎\033[0m" : " ",
                           g_room->clients[i]->username, dur_str);
        }
        pthread_rwlock_unlock(&g_room->lock);

    } else if (command_id == TNT_COMMAND_HELP) {
        manual_append_interactive_panel(output, sizeof(output), &pos,
                                        client->ui_lang);

    } else if (command_id == TNT_COMMAND_LANG) {
        ui_lang_t next_lang;

        if (!arg || arg[0] == '\0') {
            buffer_appendf(output, sizeof(output), &pos,
                           i18n_text(client->ui_lang,
                                     I18N_LANG_CURRENT_FORMAT),
                           i18n_ui_lang_code(client->ui_lang));
        } else if (i18n_try_parse_ui_lang(arg, &next_lang)) {
            client->ui_lang = next_lang;
            buffer_appendf(output, sizeof(output), &pos,
                           i18n_text(client->ui_lang,
                                     I18N_LANG_SET_FORMAT),
                           i18n_ui_lang_code(client->ui_lang));
        } else {
            buffer_appendf(output, sizeof(output), &pos,
                           i18n_text(client->ui_lang,
                                     I18N_LANG_UNSUPPORTED_FORMAT),
                           arg);
        }

    } else if (command_id == TNT_COMMAND_MSG) {
        const char *rest = arg;
        while (*rest == ' ') rest++;
        char target_name[MAX_USERNAME_LEN] = {0};
        int ti = 0;
        while (*rest && *rest != ' ' && ti < MAX_USERNAME_LEN - 1) {
            target_name[ti++] = *rest++;
        }
        while (*rest == ' ') rest++;

        if (target_name[0] == '\0' || rest[0] == '\0') {
            buffer_appendf(output, sizeof(output), &pos, "%s",
                           i18n_text(client->ui_lang, I18N_MSG_USAGE));
        } else {
            bool found = false;
            client_t *target = NULL;
            pthread_rwlock_rdlock(&g_room->lock);
            for (int i = 0; i < g_room->client_count; i++) {
                if (strcmp(g_room->clients[i]->username, target_name) == 0) {
                    target = g_room->clients[i];
                    client_addref(target);
                    found = true;
                    break;
                }
            }
            pthread_rwlock_unlock(&g_room->lock);

            if (target) {
                /* Push into recipient's inbox.  io_lock serialises so two
                 * senders to the same recipient don't tear the ring. */
                pthread_mutex_lock(&target->io_lock);
                int slot;
                if (target->whisper_inbox_count < WHISPER_INBOX_SIZE) {
                    slot = target->whisper_inbox_count++;
                } else {
                    /* FIFO evict the oldest */
                    memmove(&target->whisper_inbox[0],
                            &target->whisper_inbox[1],
                            (WHISPER_INBOX_SIZE - 1) * sizeof(whisper_t));
                    slot = WHISPER_INBOX_SIZE - 1;
                }
                target->whisper_inbox[slot].timestamp = time(NULL);
                snprintf(target->whisper_inbox[slot].from,
                         sizeof(target->whisper_inbox[slot].from),
                         "%s", client->username);
                snprintf(target->whisper_inbox[slot].content,
                         sizeof(target->whisper_inbox[slot].content),
                         "%s", rest);
                pthread_mutex_unlock(&target->io_lock);

                target->unread_whispers++;
                target->redraw_pending = true;
                /* Audible nudge — the title bar ✉ counter (UX-11 style)
                 * carries the persistent signal. */
                client_send(target, "\a", 1);
                client_release(target);
            }

            if (found) {
                buffer_appendf(output, sizeof(output), &pos,
                               i18n_text(client->ui_lang,
                                         I18N_MSG_SENT_FORMAT),
                               target_name);
            } else {
                buffer_appendf(output, sizeof(output), &pos,
                               i18n_text(client->ui_lang,
                                         I18N_MSG_USER_NOT_FOUND_FORMAT),
                               target_name);
            }
        }

    } else if (command_id == TNT_COMMAND_INBOX) {
        /* Snapshot the inbox under io_lock so a concurrent sender doesn't
         * tear what we're rendering.  Counter reset happens after copy. */
        whisper_t snapshot[WHISPER_INBOX_SIZE];
        int snap_count;
        pthread_mutex_lock(&client->io_lock);
        snap_count = client->whisper_inbox_count;
        memcpy(snapshot, client->whisper_inbox,
               snap_count * sizeof(whisper_t));
        pthread_mutex_unlock(&client->io_lock);
        client->unread_whispers = 0;

        buffer_appendf(output, sizeof(output), &pos,
                       "\033[1;36m%s\033[0m  \033[2;37m· %d\033[0m\n",
                       i18n_text(client->ui_lang, I18N_INBOX_TITLE),
                       snap_count);
        if (snap_count == 0) {
            buffer_appendf(output, sizeof(output), &pos,
                           "  \033[2;37m%s\033[0m\n",
                           i18n_text(client->ui_lang, I18N_INBOX_EMPTY));
        }
        for (int i = 0; i < snap_count; i++) {
            char ts[20];
            struct tm tmi;
            localtime_r(&snapshot[i].timestamp, &tmi);
            strftime(ts, sizeof(ts), "%m-%d %H:%M", &tmi);
            buffer_appendf(output, sizeof(output), &pos,
                           "  \033[90m%s\033[0m  \033[35m%s\033[0m: %s\n",
                           ts, snapshot[i].from, snapshot[i].content);
        }

    } else if (command_id == TNT_COMMAND_NICK) {
        const char *new_name = arg;
        while (*new_name == ' ') new_name++;

        if (new_name[0] == '\0') {
            buffer_appendf(output, sizeof(output), &pos, "%s",
                           i18n_text(client->ui_lang, I18N_NICK_USAGE));
        } else if (!is_valid_username(new_name)) {
            buffer_appendf(output, sizeof(output), &pos, "%s",
                           i18n_text(client->ui_lang, I18N_NICK_INVALID));
        } else {
            char validated_name[MAX_USERNAME_LEN];
            snprintf(validated_name, sizeof(validated_name), "%s", new_name);
            if (utf8_strlen(validated_name) > 20) {
                utf8_truncate(validated_name, 20);
            }

            /* Reject collisions with active room members.  Held under
             * wrlock so the username swap below races neither read nor
             * concurrent :nick from another client. */
            char old_name[MAX_USERNAME_LEN];
            bool taken = false;
            pthread_rwlock_wrlock(&g_room->lock);
            snprintf(old_name, sizeof(old_name), "%s", client->username);
            if (strcmp(validated_name, old_name) != 0) {
                for (int i = 0; i < g_room->client_count; i++) {
                    if (g_room->clients[i] == client) continue;
                    if (strcmp(g_room->clients[i]->username,
                               validated_name) == 0) {
                        taken = true;
                        break;
                    }
                }
            }
            if (!taken) {
                snprintf(client->username, MAX_USERNAME_LEN, "%s", validated_name);
            }
            pthread_rwlock_unlock(&g_room->lock);

            if (taken) {
                buffer_appendf(output, sizeof(output), &pos,
                               i18n_text(client->ui_lang,
                                         I18N_NICK_TAKEN_FORMAT),
                               validated_name);
            } else if (strcmp(validated_name, old_name) == 0) {
                buffer_appendf(output, sizeof(output), &pos, "%s",
                               i18n_text(client->ui_lang,
                                         I18N_NICK_UNCHANGED));
            } else {
                message_t nick_msg;
                system_message_make_nick(&nick_msg, old_name,
                                         client->username, client->ui_lang);
                room_broadcast(g_room, &nick_msg);
                message_save(&nick_msg);

                buffer_appendf(output, sizeof(output), &pos,
                               i18n_text(client->ui_lang,
                                         I18N_NICK_CHANGED_FORMAT),
                               old_name, client->username);
            }
        }

    } else if (command_id == TNT_COMMAND_LAST) {
        while (*arg == ' ') arg++;
        int n = 10;
        if (*arg != '\0') {
            char *endp;
            long val = strtol(arg, &endp, 10);
            if (*endp != '\0' || val < 1 || val > 50) {
                buffer_appendf(output, sizeof(output), &pos, "%s",
                               i18n_text(client->ui_lang, I18N_LAST_USAGE));
                goto cmd_done;
            }
            n = (int)val;
        }

        message_t *last_msgs = NULL;
        int last_count = message_load(&last_msgs, n);
        buffer_appendf(output, sizeof(output), &pos,
                       i18n_text(client->ui_lang, I18N_LAST_HEADER_FORMAT),
                       last_count);
        for (int i = 0; i < last_count; i++) {
            char ts[20];
            struct tm tmi;
            localtime_r(&last_msgs[i].timestamp, &tmi);
            strftime(ts, sizeof(ts), "%m-%d %H:%M", &tmi);
            buffer_appendf(output, sizeof(output), &pos,
                           "[%s] %s: %s\n", ts, last_msgs[i].username, last_msgs[i].content);
        }
        free(last_msgs);

    } else if (command_id == TNT_COMMAND_SEARCH) {
        const char *query = arg;
        while (*query == ' ') query++;
        if (*query == '\0') {
            buffer_appendf(output, sizeof(output), &pos, "%s",
                           i18n_text(client->ui_lang, I18N_SEARCH_USAGE));
        } else {
            message_t *found = NULL;
            int found_count = message_search(query, &found, 15);
            buffer_appendf(output, sizeof(output), &pos,
                           i18n_text(client->ui_lang,
                                     I18N_SEARCH_HEADER_FORMAT),
                           query, found_count);
            for (int i = 0; i < found_count; i++) {
                char ts[20];
                struct tm tmi;
                localtime_r(&found[i].timestamp, &tmi);
                strftime(ts, sizeof(ts), "%m-%d %H:%M", &tmi);
                buffer_appendf(output, sizeof(output), &pos,
                               "[%s] ", ts);
                append_highlighted(output, sizeof(output), &pos,
                                   found[i].username, query);
                buffer_appendf(output, sizeof(output), &pos, ": ");
                append_highlighted(output, sizeof(output), &pos,
                                   found[i].content, query);
                buffer_appendf(output, sizeof(output), &pos, "\n");
            }
            free(found);
        }

    } else if (command_id == TNT_COMMAND_MUTE_JOINS) {
        client->mute_joins = !client->mute_joins;
        buffer_appendf(output, sizeof(output), &pos,
                       i18n_text(client->ui_lang, I18N_MUTE_JOINS_FORMAT),
                       i18n_text(client->ui_lang,
                                 client->mute_joins ?
                                 I18N_MUTE_JOINS_MUTED :
                                 I18N_MUTE_JOINS_UNMUTED));

    } else if (command_id == TNT_COMMAND_QUIT) {
        client->connected = false;
        return;

    } else if (command_id == TNT_COMMAND_CLEAR) {
        buffer_appendf(output, sizeof(output), &pos, "%s",
                       i18n_text(client->ui_lang, I18N_CLEAR_DONE));
    }

cmd_done:
    snprintf(client->command_output, sizeof(client->command_output), "%s", output);
    client->command_output_scroll = 0;
    client->command_input[0] = '\0';
    tui_render_command_output(client);
}
