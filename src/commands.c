#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE  /* for strcasestr() on glibc */
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE /* for strcasestr() on macOS */
#endif
#include "commands.h"
#include "chat_room.h"
#include "client.h"
#include "common.h"
#include "i18n.h"
#include "message.h"
#include "support.h"
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

static int min3(int a, int b, int c) {
    int m = a < b ? a : b;
    return m < c ? m : c;
}

static int command_edit_distance(const char *a, const char *b) {
    size_t la = strlen(a);
    size_t lb = strlen(b);
    int prev[32];
    int curr[32];

    if (la >= 32 || lb >= 32) {
        return 99;
    }

    for (size_t j = 0; j <= lb; j++) {
        prev[j] = (int)j;
    }

    for (size_t i = 1; i <= la; i++) {
        curr[0] = (int)i;
        for (size_t j = 1; j <= lb; j++) {
            int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            curr[j] = min3(prev[j] + 1, curr[j - 1] + 1,
                           prev[j - 1] + cost);
        }
        for (size_t j = 0; j <= lb; j++) {
            prev[j] = curr[j];
        }
    }

    return prev[lb];
}

static const char *suggest_command(const char *cmd) {
    static const char *commands[] = {
        "list", "users", "who", "nick", "name", "msg", "w", "inbox",
        "last", "search", "mute-joins", "mute", "support", "guide",
        "lang", "language", "help", "commands", "clear", "cls",
        "q", "quit", "exit"
    };
    const char *best = NULL;
    int best_distance = 99;

    if (!cmd || !*cmd) {
        return NULL;
    }

    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        int distance = command_edit_distance(cmd, commands[i]);
        if (distance < best_distance) {
            best_distance = distance;
            best = commands[i];
        }
    }

    return best_distance <= 2 ? best : NULL;
}

static void append_command_help(char *output, size_t buf_size, size_t *pos,
                                help_lang_t lang) {
    if (lang == LANG_ZH) {
        buffer_appendf(output, buf_size, pos,
                       "========================================\n"
                       "    可用命令\n"
                       "========================================\n"
                       "list, users, who    - 显示在线用户\n"
                       "nick/name <name>    - 修改昵称\n"
                       "msg/w <user> <text> - 私聊用户\n"
                       "inbox               - 查看私聊历史\n"
                       "last [N]            - 查看最近 N 条消息\n"
                       "search <keyword>    - 搜索消息历史\n"
                       "mute-joins          - 切换加入/离开提示\n"
                       "support             - 显示快速支持指南\n"
                       "lang [en|zh]        - 查看或切换界面语言\n"
                       "help, commands      - 显示此帮助\n"
                       "clear, cls          - 清空命令输出\n"
                       "q, quit, exit       - 断开连接\n"
                       "上/下方向键         - 命令历史\n"
                       "========================================\n"
                       "INSERT 模式:\n"
                       "  /me <action>      - 发送动作消息\n"
                       "  @username         - 提及用户并响铃提示\n"
                       "========================================\n");
        return;
    }

    buffer_appendf(output, buf_size, pos,
                   "========================================\n"
                   "    Available Commands\n"
                   "========================================\n"
                   "list, users, who    - Show online users\n"
                   "nick/name <name>    - Change nickname\n"
                   "msg/w <user> <text> - Whisper to user (private)\n"
                   "inbox               - Show whisper history\n"
                   "last [N]            - Show last N messages\n"
                   "search <keyword>    - Search message history\n"
                   "mute-joins          - Toggle join/leave notices\n"
                   "support             - Show quick support guide\n"
                   "lang [en|zh]        - Show or switch UI language\n"
                   "help, commands      - Show this help\n"
                   "clear, cls          - Clear command output\n"
                   "q, quit, exit       - Disconnect\n"
                   "Up/Down arrows      - Command history\n"
                   "========================================\n"
                   "In INSERT mode:\n"
                   "  /me <action>      - Send action message\n"
                   "  @username         - Mention (bell notify)\n"
                   "========================================\n");
}

void commands_dispatch(client_t *client) {
    char cmd_buf[256];
    strncpy(cmd_buf, client->command_input, sizeof(cmd_buf) - 1);
    cmd_buf[sizeof(cmd_buf) - 1] = '\0';
    char *cmd = cmd_buf;
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

    if (strcmp(cmd, "list") == 0 || strcmp(cmd, "users") == 0 ||
        strcmp(cmd, "who") == 0) {
        pthread_rwlock_rdlock(&g_room->lock);
        int total = g_room->client_count;
        buffer_appendf(output, sizeof(output), &pos,
                       "\033[1;36m在线用户 · online\033[0m  "
                       "\033[2;37m· %d\033[0m\n", total);

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

    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "commands") == 0) {
        append_command_help(output, sizeof(output), &pos, client->help_lang);

    } else if (strcmp(cmd, "support") == 0 || strcmp(cmd, "guide") == 0) {
        support_append_interactive_panel(output, sizeof(output), &pos,
                                         client->help_lang);

    } else if (strcmp(cmd, "lang") == 0 || strcmp(cmd, "language") == 0 ||
               strncmp(cmd, "lang ", 5) == 0 ||
               strncmp(cmd, "language ", 9) == 0) {
        char *arg = NULL;
        help_lang_t next_lang;

        if (strncmp(cmd, "lang ", 5) == 0) {
            arg = cmd + 5;
        } else if (strncmp(cmd, "language ", 9) == 0) {
            arg = cmd + 9;
        }

        if (!arg || arg[0] == '\0') {
            if (client->help_lang == LANG_ZH) {
                buffer_appendf(output, sizeof(output), &pos,
                               "当前语言: %s\n"
                               "用法: lang <en|zh>\n",
                               i18n_lang_code(client->help_lang));
            } else {
                buffer_appendf(output, sizeof(output), &pos,
                               "Current language: %s\n"
                               "Usage: lang <en|zh>\n",
                               i18n_lang_code(client->help_lang));
            }
        } else if (i18n_try_parse_lang(arg, &next_lang)) {
            client->help_lang = next_lang;
            if (client->help_lang == LANG_ZH) {
                buffer_appendf(output, sizeof(output), &pos,
                               "语言已切换为: %s\n",
                               i18n_lang_code(client->help_lang));
            } else {
                buffer_appendf(output, sizeof(output), &pos,
                               "Language set to: %s\n",
                               i18n_lang_code(client->help_lang));
            }
        } else {
            if (client->help_lang == LANG_ZH) {
                buffer_appendf(output, sizeof(output), &pos,
                               "不支持的语言: %s\n"
                               "用法: lang <en|zh>\n", arg);
            } else {
                buffer_appendf(output, sizeof(output), &pos,
                               "Unsupported language: %s\n"
                               "Usage: lang <en|zh>\n", arg);
            }
        }

    } else if (strncmp(cmd, "msg ", 4) == 0 || strncmp(cmd, "w ", 2) == 0) {
        char *rest = (cmd[0] == 'w') ? cmd + 2 : cmd + 4;
        while (*rest == ' ') rest++;
        char target_name[MAX_USERNAME_LEN] = {0};
        int ti = 0;
        while (*rest && *rest != ' ' && ti < MAX_USERNAME_LEN - 1) {
            target_name[ti++] = *rest++;
        }
        while (*rest == ' ') rest++;

        if (target_name[0] == '\0' || rest[0] == '\0') {
            buffer_appendf(output, sizeof(output), &pos,
                           "Usage: msg <username> <message>\n"
                           "       w <username> <message>\n");
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
                               "Whisper sent to %s\n", target_name);
            } else {
                buffer_appendf(output, sizeof(output), &pos,
                               "User '%s' not found\n", target_name);
            }
        }

    } else if (strcmp(cmd, "inbox") == 0) {
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
                       "\033[1;36m悄悄话 · whispers\033[0m  "
                       "\033[2;37m· %d\033[0m\n", snap_count);
        if (snap_count == 0) {
            buffer_appendf(output, sizeof(output), &pos,
                           "  \033[2;37m(空)\033[0m\n");
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

    } else if (strncmp(cmd, "nick ", 5) == 0 || strncmp(cmd, "name ", 5) == 0) {
        char *new_name = cmd + 5;
        while (*new_name == ' ') new_name++;

        if (new_name[0] == '\0') {
            buffer_appendf(output, sizeof(output), &pos,
                           "Usage: nick <new_username>\n");
        } else if (!is_valid_username(new_name)) {
            buffer_appendf(output, sizeof(output), &pos,
                           "Invalid username\n");
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
                               "Nickname '%s' is already taken\n",
                               validated_name);
            } else if (strcmp(validated_name, old_name) == 0) {
                buffer_appendf(output, sizeof(output), &pos,
                               "Nickname unchanged\n");
            } else {
                message_t nick_msg = { .timestamp = time(NULL) };
                snprintf(nick_msg.username, MAX_USERNAME_LEN, "系统");
                snprintf(nick_msg.content, MAX_MESSAGE_LEN,
                         "%s 更名为 %s", old_name, client->username);
                room_broadcast(g_room, &nick_msg);
                message_save(&nick_msg);

                buffer_appendf(output, sizeof(output), &pos,
                               "Nickname changed: %s -> %s\n",
                               old_name, client->username);
            }
        }

    } else if (strncmp(cmd, "last", 4) == 0 && (cmd[4] == ' ' || cmd[4] == '\0')) {
        char *arg = cmd + 4;
        while (*arg == ' ') arg++;
        int n = 10;
        if (*arg != '\0') {
            char *endp;
            long val = strtol(arg, &endp, 10);
            if (*endp != '\0' || val < 1 || val > 50) {
                buffer_appendf(output, sizeof(output), &pos,
                               "Usage: last [N]  (N: 1-50, default 10)\n");
                goto cmd_done;
            }
            n = (int)val;
        }

        message_t *last_msgs = NULL;
        int last_count = message_load(&last_msgs, n);
        buffer_appendf(output, sizeof(output), &pos,
                       "--- Last %d message(s) ---\n", last_count);
        for (int i = 0; i < last_count; i++) {
            char ts[20];
            struct tm tmi;
            localtime_r(&last_msgs[i].timestamp, &tmi);
            strftime(ts, sizeof(ts), "%m-%d %H:%M", &tmi);
            buffer_appendf(output, sizeof(output), &pos,
                           "[%s] %s: %s\n", ts, last_msgs[i].username, last_msgs[i].content);
        }
        free(last_msgs);

    } else if (strncmp(cmd, "search ", 7) == 0) {
        char *query = cmd + 7;
        while (*query == ' ') query++;
        if (*query == '\0') {
            buffer_appendf(output, sizeof(output), &pos,
                           "Usage: search <keyword>\n");
        } else {
            message_t *found = NULL;
            int found_count = message_search(query, &found, 15);
            buffer_appendf(output, sizeof(output), &pos,
                           "--- Search: \"%s\" (%d match(es)) ---\n", query, found_count);
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

    } else if (strcmp(cmd, "mute-joins") == 0 || strcmp(cmd, "mute") == 0) {
        client->mute_joins = !client->mute_joins;
        buffer_appendf(output, sizeof(output), &pos,
                       "Join/leave notifications: %s\n",
                       client->mute_joins ? "muted" : "unmuted");

    } else if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0 ||
               strcmp(cmd, "exit") == 0) {
        client->connected = false;
        return;

    } else if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0) {
        buffer_appendf(output, sizeof(output), &pos, "Command output cleared\n");

    } else if (cmd[0] == '\0') {
        /* Empty command */
        client->mode = MODE_NORMAL;
        client->command_input[0] = '\0';
        tui_render_screen(client);
        return;

    } else {
        const char *suggestion = suggest_command(cmd);
        buffer_appendf(output, sizeof(output), &pos,
                       client->help_lang == LANG_ZH ?
                       "未知命令: %s\n" : "Unknown command: %s\n", cmd);
        if (suggestion) {
            if (client->help_lang == LANG_ZH) {
                buffer_appendf(output, sizeof(output), &pos,
                               "你是想输入 :%s 吗?\n", suggestion);
            } else {
                buffer_appendf(output, sizeof(output), &pos,
                               "Did you mean :%s?\n", suggestion);
            }
        }
        buffer_appendf(output, sizeof(output), &pos,
                       client->help_lang == LANG_ZH ?
                       "输入 :support 查看引导，或 :help 查看命令\n" :
                       "Type :support for guidance or :help for commands\n");
    }

cmd_done:
    buffer_appendf(output, sizeof(output), &pos,
                   client->help_lang == LANG_ZH ?
                   "\n按任意键继续..." :
                   "\nPress any key to continue...");

    snprintf(client->command_output, sizeof(client->command_output), "%s", output);
    client->command_input[0] = '\0';
    tui_render_command_output(client);
}
