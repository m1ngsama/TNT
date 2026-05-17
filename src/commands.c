#include "commands.h"
#include "chat_room.h"
#include "common.h"
#include "message.h"
#include "tui.h"
#include "utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
        buffer_appendf(output, sizeof(output), &pos,
                       "========================================\n"
                       "     Online Users / 在线用户\n"
                       "========================================\n");

        pthread_rwlock_rdlock(&g_room->lock);
        buffer_appendf(output, sizeof(output), &pos,
                       "Total / 总数: %d\n"
                       "----------------------------------------\n",
                       g_room->client_count);

        time_t now = time(NULL);
        for (int i = 0; i < g_room->client_count; i++) {
            char marker = (g_room->clients[i] == client) ? '*' : ' ';
            int dur = (int)(now - g_room->clients[i]->connect_time);
            char dur_str[32];
            if (dur < 60) {
                snprintf(dur_str, sizeof(dur_str), "%ds", dur);
            } else if (dur < 3600) {
                snprintf(dur_str, sizeof(dur_str), "%dm", dur / 60);
            } else {
                snprintf(dur_str, sizeof(dur_str), "%dh%dm", dur / 3600, (dur % 3600) / 60);
            }
            buffer_appendf(output, sizeof(output), &pos,
                           "%c %d. %s (%s)\n", marker, i + 1,
                           g_room->clients[i]->username, dur_str);
        }

        pthread_rwlock_unlock(&g_room->lock);

        buffer_appendf(output, sizeof(output), &pos,
                       "========================================\n"
                       "* = you / 你\n");

    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "commands") == 0) {
        buffer_appendf(output, sizeof(output), &pos,
                       "========================================\n"
                       "    Available Commands / 可用命令\n"
                       "========================================\n"
                       "list, users, who    - Show online users\n"
                       "nick/name <name>    - Change nickname\n"
                       "msg/w <user> <text> - Whisper to user\n"
                       "last [N]            - Show last N messages\n"
                       "search <keyword>    - Search message history\n"
                       "mute-joins          - Toggle join/leave notices\n"
                       "help, commands      - Show this help\n"
                       "clear, cls          - Clear command output\n"
                       "q, quit, exit       - Disconnect\n"
                       "Up/Down arrows      - Command history\n"
                       "========================================\n"
                       "In INSERT mode:\n"
                       "  /me <action>      - Send action message\n"
                       "  @username         - Mention (bell notify)\n"
                       "========================================\n");

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
                char whisper[MAX_MESSAGE_LEN];
                snprintf(whisper, sizeof(whisper),
                         "\r\n\033[35m[whisper from %s]: %s\033[0m\r\n",
                         client->username, rest);
                client_send(target, whisper, strlen(whisper));
                target->redraw_pending = true;
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

            char old_name[MAX_USERNAME_LEN];
            pthread_rwlock_wrlock(&g_room->lock);
            snprintf(old_name, sizeof(old_name), "%s", client->username);
            snprintf(client->username, MAX_USERNAME_LEN, "%s", validated_name);
            pthread_rwlock_unlock(&g_room->lock);

            message_t nick_msg = { .timestamp = time(NULL) };
            snprintf(nick_msg.username, MAX_USERNAME_LEN, "系统");
            snprintf(nick_msg.content, MAX_MESSAGE_LEN,
                     "%s 更名为 %s", old_name, client->username);
            room_broadcast(g_room, &nick_msg);
            message_save(&nick_msg);

            buffer_appendf(output, sizeof(output), &pos,
                           "Nickname changed: %s -> %s\n", old_name, client->username);
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
                               "[%s] %s: %s\n", ts, found[i].username, found[i].content);
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
        buffer_appendf(output, sizeof(output), &pos,
                       "Unknown command: %s\n"
                       "Type 'help' for available commands\n", cmd);
    }

cmd_done:
    buffer_appendf(output, sizeof(output), &pos,
                   "\nPress any key to continue...");

    snprintf(client->command_output, sizeof(client->command_output), "%s", output);
    client->command_input[0] = '\0';
    tui_render_command_output(client);
}
