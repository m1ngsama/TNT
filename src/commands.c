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
#include "identity.h"
#include "keymap.h"
#include "manual.h"
#include "message.h"
#include "module_protocol.h"
#include "module_runtime.h"
#include "richtext.h"
#include "system_message.h"
#include "theme.h"
#include "tui.h"
#include "utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static void append_highlighted(char *output, size_t buf_size, size_t *pos,
                               const char *visible,
                               const richtext_run_t *runs, size_t run_count,
                               const char *needle) {
    size_t len = strlen(visible);
    size_t nlen = needle ? strlen(needle) : 0;
    size_t at = 0;

    while (nlen > 0 && at < len) {
        const char *hit = strcasestr(visible + at, needle);
        if (!hit) {
            break;
        }
        size_t hit_at = (size_t)(hit - visible);
        richtext_append_styled(output, buf_size, pos, visible, at, hit_at,
                               runs, run_count, "");
        buffer_append_bytes(output, buf_size, pos, "\033[7;33m", 7);
        buffer_append_bytes(output, buf_size, pos, hit, nlen);
        buffer_append_bytes(output, buf_size, pos, "\033[0m", 4);
        at = hit_at + nlen;
    }
    richtext_append_styled(output, buf_size, pos, visible, at, len,
                           runs, run_count, "");
}

static bool append_message_row(char *output, size_t buf_size, size_t *pos,
                               const message_t *msg, const char *needle) {
    size_t mark = *pos;
    char visible[MAX_MESSAGE_LEN];
    richtext_run_t runs[RICHTEXT_MAX_RUNS];
    size_t run_count = richtext_parse(msg->content, visible, sizeof(visible),
                                      NULL, runs, RICHTEXT_MAX_RUNS);
    char ts[20];
    struct tm tmi;

    localtime_r(&msg->timestamp, &tmi);
    strftime(ts, sizeof(ts), "%m-%d %H:%M", &tmi);
    buffer_appendf(output, buf_size, pos, "[%s] ", ts);
    append_highlighted(output, buf_size, pos, msg->username, NULL, 0, needle);
    buffer_appendf(output, buf_size, pos, ": ");
    append_highlighted(output, buf_size, pos, visible, runs, run_count,
                       needle);
    buffer_appendf(output, buf_size, pos, "\n");

    /* Drop a row that did not fit whole, or the cut can split an escape. */
    if (*pos >= buf_size - 1) {
        *pos = mark;
        output[mark] = '\0';
        return false;
    }
    return true;
}

static void append_command_usage(char *output, size_t buf_size, size_t *pos,
                                 tnt_command_id_t id, ui_lang_t lang) {
    command_catalog_append_usage(output, buf_size, pos, id, lang);
}

static bool message_visible_for_client(const client_t *client,
                                       const message_t *msg) {
    return !client || !client->mute_joins ||
           !system_message_is_join_leave(msg);
}

static void client_append_whisper(client_t *owner, const char *from,
                                  const char *to, const char *content,
                                  bool outgoing, bool count_unread) {
    if (!owner || !from || !to || !content) return;

    pthread_mutex_lock(&owner->whisper_lock);
    int slot;
    if (owner->whisper_inbox_count < WHISPER_INBOX_SIZE) {
        slot = owner->whisper_inbox_count++;
    } else {
        memmove(&owner->whisper_inbox[0],
                &owner->whisper_inbox[1],
                (WHISPER_INBOX_SIZE - 1) * sizeof(whisper_t));
        slot = WHISPER_INBOX_SIZE - 1;
    }

    owner->whisper_inbox[slot].timestamp = time(NULL);
    snprintf(owner->whisper_inbox[slot].from,
             sizeof(owner->whisper_inbox[slot].from), "%s", from);
    snprintf(owner->whisper_inbox[slot].to,
             sizeof(owner->whisper_inbox[slot].to), "%s", to);
    snprintf(owner->whisper_inbox[slot].content,
             sizeof(owner->whisper_inbox[slot].content), "%s", content);
    owner->whisper_inbox[slot].outgoing = outgoing;
    owner->whisper_inbox[slot].unread = count_unread;
    snprintf(owner->last_whisper_peer, sizeof(owner->last_whisper_peer), "%s",
             outgoing ? to : from);
    if (count_unread) {
        owner->unread_whispers++;
    }
    pthread_mutex_unlock(&owner->whisper_lock);
}

static void send_private_message(client_t *client, const char *target_name,
                                 const char *content, char *output,
                                 size_t buf_size, size_t *pos) {
    bool found = false;
    bool verified;
    client_t *target = NULL;

    pthread_rwlock_rdlock(&g_room->lock);
    verified = client->verified;
    for (int i = 0; i < g_room->client_count; i++) {
        if (strcasecmp(g_room->clients[i]->username, target_name) == 0) {
            target = g_room->clients[i];
            client_addref(target);
            found = true;
            break;
        }
    }
    pthread_rwlock_unlock(&g_room->lock);

    if (target) {
        client_append_whisper(target, client->username, target_name,
                              content, false, true);
        if (target != client) {
            client_append_whisper(client, client->username, target_name,
                                  content, true, false);
        }

        /* Audible nudge: the title bar whisper counter carries the
         * persistent signal without cross-client SSH writes. */
        client_queue_bell(target);
        client_release(target);
    } else if (tnt_module_runtime_send_private(client->username, target_name,
                                               content, verified)) {
        client_append_whisper(client, client->username, target_name, content,
                              true, false);
        found = true;
    }

    if (found) {
        buffer_appendf(output, buf_size, pos,
                       i18n_text(client->ui_lang, I18N_MSG_SENT_FORMAT),
                       target_name);
    } else {
        buffer_appendf(output, buf_size, pos,
                       i18n_text(client->ui_lang,
                                 I18N_MSG_USER_NOT_FOUND_FORMAT),
                       target_name);
    }
}

void commands_deliver_private(const char *from, const char *to,
                              const char *plain_text) {
    client_t **targets = NULL;
    int target_count = 0;

    pthread_rwlock_rdlock(&g_room->lock);
    if (g_room->client_count > 0) {
        targets = calloc((size_t)g_room->client_count, sizeof(*targets));
    }
    for (int i = 0; targets && i < g_room->client_count; i++) {
        if (strcasecmp(g_room->clients[i]->username, to) == 0) {
            client_addref(g_room->clients[i]);
            targets[target_count++] = g_room->clients[i];
        }
    }
    pthread_rwlock_unlock(&g_room->lock);

    for (int i = 0; i < target_count; i++) {
        client_append_whisper(targets[i], from, to, plain_text, false, true);
        client_queue_bell(targets[i]);
        client_release(targets[i]);
    }
    free(targets);
}

static void append_inbox_output(client_t *client, char *output,
                                size_t buf_size, size_t *pos) {
    whisper_t snapshot[WHISPER_INBOX_SIZE];
    int snap_count;
    int unread_count;

    pthread_mutex_lock(&client->whisper_lock);
    snap_count = client->whisper_inbox_count;
    unread_count = client->unread_whispers;
    memcpy(snapshot, client->whisper_inbox,
           snap_count * sizeof(whisper_t));
    for (int i = 0; i < snap_count; i++) {
        client->whisper_inbox[i].unread = false;
    }
    client->unread_whispers = 0;
    pthread_mutex_unlock(&client->whisper_lock);

    buffer_appendf(output, buf_size, pos,
                   "%s%s\033[0m  \033[2;37m· %d",
                   theme_resolve(client->theme_index)->accent_bold,
                   i18n_text(client->ui_lang, I18N_INBOX_TITLE),
                   snap_count);
    if (unread_count > 0) {
        buffer_appendf(output, buf_size, pos,
                       " · ");
        buffer_appendf(output, buf_size, pos,
                       i18n_text(client->ui_lang,
                                 I18N_INBOX_UNREAD_FORMAT),
                       unread_count);
    }
    buffer_appendf(output, buf_size, pos, "\033[0m\n");
    if (snap_count == 0) {
        buffer_appendf(output, buf_size, pos,
                       "  \033[2;37m%s\033[0m\n",
                       i18n_text(client->ui_lang, I18N_INBOX_EMPTY));
    }
    for (int i = snap_count - 1; i >= 0; i--) {
        char ts[20];
        char peer[MAX_USERNAME_LEN + 16];
        const char *marker = snapshot[i].unread ? "\033[1;35m*\033[0m" : " ";
        struct tm tmi;
        localtime_r(&snapshot[i].timestamp, &tmi);
        strftime(ts, sizeof(ts), "%m-%d %H:%M", &tmi);
        if (snapshot[i].outgoing) {
            snprintf(peer, sizeof(peer),
                     i18n_text(client->ui_lang,
                               I18N_INBOX_SENT_TO_FORMAT),
                     snapshot[i].to);
        } else {
            snprintf(peer, sizeof(peer), "%s", snapshot[i].from);
        }
        buffer_appendf(output, buf_size, pos,
                       "  %s \033[90m%s\033[0m  \033[35m%s\033[0m: %s\n",
                       marker, ts, peer, snapshot[i].content);
    }
}

static void clear_inbox(client_t *client) {
    pthread_mutex_lock(&client->whisper_lock);
    memset(client->whisper_inbox, 0, sizeof(client->whisper_inbox));
    client->whisper_inbox_count = 0;
    client->unread_whispers = 0;
    client->last_whisper_peer[0] = '\0';
    pthread_mutex_unlock(&client->whisper_lock);
}

int commands_rename(client_t *client, const char *new_name, bool verified) {
    char old_name[MAX_USERNAME_LEN];
    bool taken = false;
    bool renamed;

    pthread_rwlock_wrlock(&g_room->lock);
    snprintf(old_name, sizeof(old_name), "%s", client->username);
    renamed = strcmp(new_name, old_name) != 0;
    if (renamed && !verified) {
        for (int i = 0; i < g_room->client_count; i++) {
            if (g_room->clients[i] != client &&
                strcasecmp(g_room->clients[i]->username, new_name) == 0) {
                taken = true;
                break;
            }
        }
    }
    if (!taken) {
        client->verified = verified;
        snprintf(client->username, MAX_USERNAME_LEN, "%s", new_name);
        if (g_room->presence_notifier && renamed) {
            g_room->presence_notifier(old_name, false);
            g_room->presence_notifier(client->username, true);
        }
    }
    pthread_rwlock_unlock(&g_room->lock);

    if (taken) {
        return -1;
    }
    if (!renamed) {
        return 1;
    }

    message_t nick_msg;
    system_message_make_nick(&nick_msg, old_name, client->username,
                             client->ui_lang);
    room_broadcast(g_room, &nick_msg);
    message_save(&nick_msg);
    tnt_module_runtime_publish_message_created(&nick_msg, false);
    return 0;
}

void commands_become_anonymous(client_t *client) {
    char name[MAX_USERNAME_LEN] = "anonymous";

    for (int n = 2; commands_rename(client, name, false) < 0 && n < 1000; n++) {
        snprintf(name, sizeof(name), "anonymous%d", n);
    }
}

bool commands_refresh_active_output(client_t *client) {
    char output[MAX_COMMAND_OUTPUT_LEN] = {0};
    size_t pos = 0;

    if (!client || client->command_output_kind != TNT_COMMAND_OUTPUT_INBOX) {
        return false;
    }

    append_inbox_output(client, output, sizeof(output), &pos);
    snprintf(client->command_output, sizeof(client->command_output), "%s",
             output);
    client->command_output_scroll = 0;
    return true;
}

void commands_dispatch(client_t *client) {
    char cmd_buf[256];
    strncpy(cmd_buf, client->command_input, sizeof(cmd_buf) - 1);
    cmd_buf[sizeof(cmd_buf) - 1] = '\0';
    char *cmd = cmd_buf;
    char output[MAX_COMMAND_OUTPUT_LEN] = {0};
    tnt_command_output_kind_t output_kind = TNT_COMMAND_OUTPUT_GENERIC;
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
    if (cmd[0] == ':') {
        cmd++;
        while (*cmd == ' ') cmd++;
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

    if (!command_catalog_args_valid(command_id, arg)) {
        append_command_usage(output, sizeof(output), &pos, command_id,
                             client->ui_lang);
        goto cmd_done;
    }

    if (command_id == TNT_COMMAND_USERS) {
        const theme_t *theme = theme_resolve(client->theme_index);
        char self_gutter[32];
        snprintf(self_gutter, sizeof(self_gutter), "%s▎\033[0m", theme->accent);

        char (*remote)[MAX_USERNAME_LEN] =
            calloc(TNT_MODULE_REMOTE_USERS_MAX, sizeof(*remote));
        int remote_count = remote ? tnt_module_runtime_copy_remote_users(
                                        remote, TNT_MODULE_REMOTE_USERS_MAX)
                                  : 0;

        pthread_rwlock_rdlock(&g_room->lock);
        int total = g_room->client_count;
        buffer_appendf(output, sizeof(output), &pos,
                       "%s%s\033[0m  \033[2;37m· %d\033[0m\n",
                       theme->accent_bold,
                       i18n_text(client->ui_lang, I18N_USERS_TITLE),
                       total + remote_count);

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
                           is_self ? self_gutter : " ",
                           g_room->clients[i]->username, dur_str);
        }
        pthread_rwlock_unlock(&g_room->lock);
        for (int i = 0; i < remote_count; i++) {
            buffer_appendf(output, sizeof(output), &pos,
                           "   \033[37m%s\033[0m  \033[2;37m· %s\033[0m\n",
                           remote[i],
                           i18n_text(client->ui_lang, I18N_USERS_APP));
        }
        free(remote);

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
            append_command_usage(output, sizeof(output), &pos,
                                 TNT_COMMAND_MSG, client->ui_lang);
        } else {
            send_private_message(client, target_name, rest, output,
                                 sizeof(output), &pos);
        }

    } else if (command_id == TNT_COMMAND_REPLY) {
        const char *message = arg;
        char target_name[MAX_USERNAME_LEN] = {0};

        while (*message == ' ') message++;
        if (message[0] == '\0') {
            append_command_usage(output, sizeof(output), &pos,
                                 TNT_COMMAND_REPLY, client->ui_lang);
        } else {
            pthread_mutex_lock(&client->whisper_lock);
            snprintf(target_name, sizeof(target_name), "%s",
                     client->last_whisper_peer);
            pthread_mutex_unlock(&client->whisper_lock);

            if (target_name[0] == '\0') {
                buffer_appendf(output, sizeof(output), &pos, "%s",
                               i18n_text(client->ui_lang,
                                         I18N_REPLY_NO_TARGET));
            } else {
                send_private_message(client, target_name, message, output,
                                     sizeof(output), &pos);
            }
        }

    } else if (command_id == TNT_COMMAND_INBOX) {
        const char *inbox_arg = arg;
        while (*inbox_arg == ' ') inbox_arg++;
        if (strcmp(inbox_arg, "clear") == 0) {
            clear_inbox(client);
            output_kind = TNT_COMMAND_OUTPUT_INBOX;
            buffer_appendf(output, sizeof(output), &pos, "%s",
                           i18n_text(client->ui_lang,
                                     I18N_INBOX_CLEARED));
            buffer_appendf(output, sizeof(output), &pos, "\n");
            append_inbox_output(client, output, sizeof(output), &pos);
        } else {
            output_kind = TNT_COMMAND_OUTPUT_INBOX;
            append_inbox_output(client, output, sizeof(output), &pos);
        }

    } else if (command_id == TNT_COMMAND_NICK) {
        const char *new_name = arg;
        while (*new_name == ' ') new_name++;

        if (new_name[0] == '\0') {
            append_command_usage(output, sizeof(output), &pos,
                                 TNT_COMMAND_NICK, client->ui_lang);
        } else if (client->verified) {
            buffer_appendf(output, sizeof(output), &pos,
                           i18n_text(client->ui_lang,
                                     I18N_NICK_SIGNED_IN_FORMAT),
                           client->username);
        } else if (!is_valid_username(new_name)) {
            buffer_appendf(output, sizeof(output), &pos, "%s",
                           i18n_text(client->ui_lang, I18N_NICK_INVALID));
        } else {
            char validated_name[MAX_USERNAME_LEN];
            snprintf(validated_name, sizeof(validated_name), "%s", new_name);
            if (utf8_strlen(validated_name) > 20) {
                utf8_truncate(validated_name, 20);
            }

            char old_name[MAX_USERNAME_LEN];
            snprintf(old_name, sizeof(old_name), "%s", client->username);

            if (identity_is_reserved(validated_name)) {
                buffer_appendf(output, sizeof(output), &pos,
                               i18n_text(client->ui_lang,
                                         I18N_NICK_RESERVED_FORMAT),
                               validated_name);
            } else {
                int rc = commands_rename(client, validated_name, false);

                if (rc < 0) {
                    buffer_appendf(output, sizeof(output), &pos,
                                   i18n_text(client->ui_lang,
                                             I18N_NICK_TAKEN_FORMAT),
                                   validated_name);
                } else if (rc > 0) {
                    buffer_appendf(output, sizeof(output), &pos, "%s",
                                   i18n_text(client->ui_lang,
                                             I18N_NICK_UNCHANGED));
                } else {
                    buffer_appendf(output, sizeof(output), &pos,
                                   i18n_text(client->ui_lang,
                                             I18N_NICK_CHANGED_FORMAT),
                                   old_name, client->username);
                }
            }
        }

    } else if (command_id == TNT_COMMAND_LOGIN) {
        if (client->verified) {
            buffer_appendf(output, sizeof(output), &pos,
                           i18n_text(client->ui_lang,
                                     I18N_LOGIN_ALREADY_FORMAT),
                           client->username);
        } else if (tnt_module_runtime_request_identity(client->session_id,
                                                       client->username)) {
            buffer_appendf(output, sizeof(output), &pos, "%s",
                           i18n_text(client->ui_lang, I18N_LOGIN_OPENING));
        } else {
            buffer_appendf(output, sizeof(output), &pos, "%s",
                           i18n_text(client->ui_lang,
                                     I18N_LOGIN_UNAVAILABLE));
        }

    } else if (command_id == TNT_COMMAND_LOGOUT) {
        if (!client->verified) {
            buffer_appendf(output, sizeof(output), &pos, "%s",
                           i18n_text(client->ui_lang,
                                     I18N_LOGOUT_NOT_SIGNED_IN));
        } else {
            if (client->key_fingerprint[0] != '\0') {
                (void)identity_forget_key(client->key_fingerprint);
            }
            commands_become_anonymous(client);
            buffer_appendf(output, sizeof(output), &pos,
                           i18n_text(client->ui_lang,
                                     I18N_LOGOUT_DONE_FORMAT),
                           client->username);
        }

    } else if (command_id == TNT_COMMAND_LAST) {
        while (*arg == ' ') arg++;
        int n = 10;
        if (*arg != '\0') {
            char *endp;
            long val = strtol(arg, &endp, 10);
            if (*endp != '\0' || val < 1 || val > 50) {
                append_command_usage(output, sizeof(output), &pos,
                                     TNT_COMMAND_LAST, client->ui_lang);
                goto cmd_done;
            }
            n = (int)val;
        }

        message_t *last_msgs = NULL;
        int load_count = message_load(&last_msgs,
                                      client->mute_joins ? MAX_MESSAGES : n);
        int visible_count = 0;
        for (int i = 0; i < load_count; i++) {
            if (message_visible_for_client(client, &last_msgs[i])) {
                last_msgs[visible_count++] = last_msgs[i];
            }
        }
        int start = visible_count > n ? visible_count - n : 0;
        int last_count = visible_count - start;
        buffer_appendf(output, sizeof(output), &pos,
                       i18n_text(client->ui_lang, I18N_LAST_HEADER_FORMAT),
                       last_count);
        if (last_count == 0) {
            buffer_appendf(output, sizeof(output), &pos, "%s",
                           i18n_text(client->ui_lang, I18N_LAST_EMPTY));
        }
        for (int i = 0; i < last_count; i++) {
            if (!append_message_row(output, sizeof(output), &pos,
                                    &last_msgs[start + i], NULL)) {
                break;
            }
        }
        free(last_msgs);

    } else if (command_id == TNT_COMMAND_SEARCH) {
        const char *query = arg;
        while (*query == ' ') query++;
        if (*query == '\0') {
            append_command_usage(output, sizeof(output), &pos,
                                 TNT_COMMAND_SEARCH, client->ui_lang);
        } else {
            message_t *found = NULL;
            int search_limit = client->mute_joins ? MAX_MESSAGES : 15;
            int found_count = message_search(query, &found, search_limit);
            int visible_count = 0;
            for (int i = 0; i < found_count; i++) {
                if (message_visible_for_client(client, &found[i])) {
                    found[visible_count++] = found[i];
                }
            }
            int start = visible_count > 15 ? visible_count - 15 : 0;
            int display_count = visible_count - start;
            buffer_appendf(output, sizeof(output), &pos,
                           i18n_text(client->ui_lang,
                                     I18N_SEARCH_HEADER_FORMAT),
                           query, display_count);
            if (display_count == 0) {
                buffer_appendf(output, sizeof(output), &pos, "%s",
                               i18n_text(client->ui_lang,
                                         I18N_SEARCH_EMPTY));
            }
            for (int i = 0; i < display_count; i++) {
                if (!append_message_row(output, sizeof(output), &pos,
                                        &found[start + i], query)) {
                    break;
                }
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

    } else if (command_id == TNT_COMMAND_KEYMAP) {
        const char *want = arg;
        while (*want == ' ') want++;

        /* "off" returns to the plain keys; anything else asks for vim. */
        client->keymap = (strcmp(want, "off") == 0) ? TNT_KEYMAP_DEFAULT
                                                    : TNT_KEYMAP_VIM;
        if (!tnt_keymap_uses_modes(client->keymap)) {
            /* The modal states are unreachable from here on, so do not leave
             * the session parked in one of them. */
            client->mode = MODE_INSERT;
        }
        buffer_appendf(output, sizeof(output), &pos,
                       i18n_text(client->ui_lang, I18N_KEYMAP_SWITCHED_FORMAT),
                       tnt_keymap_name(client->keymap));
    } else if (command_id == TNT_COMMAND_THEME) {
        const char *name = arg;
        while (*name == ' ') name++;

        if (name[0] == '\0') {
            char names[256] = {0};
            size_t npos = 0;
            theme_append_names(names, sizeof(names), &npos);
            buffer_appendf(output, sizeof(output), &pos,
                           i18n_text(client->ui_lang,
                                     I18N_THEME_CURRENT_FORMAT),
                           theme_resolve(client->theme_index)->name, names);
        } else {
            const theme_t *chosen = theme_find(name);
            if (chosen) {
                client->theme_index = (int)(chosen - theme_at(0));
                buffer_appendf(output, sizeof(output), &pos,
                               i18n_text(client->ui_lang,
                                         I18N_THEME_SET_FORMAT),
                               chosen->name);
            } else {
                char names[256] = {0};
                size_t npos = 0;
                theme_append_names(names, sizeof(names), &npos);
                buffer_appendf(output, sizeof(output), &pos,
                               i18n_text(client->ui_lang,
                                         I18N_THEME_UNSUPPORTED_FORMAT),
                               name, names);
            }
        }

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
    client->command_output_kind = output_kind;
    client->command_input[0] = '\0';
    tui_render_command_output(client);
}
