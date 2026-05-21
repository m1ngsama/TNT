#include "exec.h"
#include "chat_room.h"
#include "client.h"
#include "common.h"
#include "input.h"
#include "message.h"
#include "ratelimit.h"
#include "support.h"
#include "utf8.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* `notify_mentions` is shared with the interactive INSERT-mode send path.
 * Declared in input.h. */

static void format_timestamp_utc(time_t ts, char *buffer, size_t buf_size) {
    struct tm tm_info;

    if (!buffer || buf_size == 0) {
        return;
    }

    gmtime_r(&ts, &tm_info);
    strftime(buffer, buf_size, "%Y-%m-%dT%H:%M:%SZ", &tm_info);
}

static void trim_ascii_whitespace(char *text) {
    char *start;
    char *end;

    if (!text || text[0] == '\0') {
        return;
    }

    start = text;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }

    if (text[0] == '\0') {
        return;
    }

    end = text + strlen(text) - 1;
    while (end >= text && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
}

static void json_append_string(char *buffer, size_t buf_size, size_t *pos,
                               const char *text) {
    const unsigned char *p = (const unsigned char *)(text ? text : "");

    buffer_append_bytes(buffer, buf_size, pos, "\"", 1);

    while (*p && *pos < buf_size - 1) {
        char escaped[7];

        switch (*p) {
            case '\\':
                buffer_append_bytes(buffer, buf_size, pos, "\\\\", 2);
                break;
            case '"':
                buffer_append_bytes(buffer, buf_size, pos, "\\\"", 2);
                break;
            case '\n':
                buffer_append_bytes(buffer, buf_size, pos, "\\n", 2);
                break;
            case '\r':
                buffer_append_bytes(buffer, buf_size, pos, "\\r", 2);
                break;
            case '\t':
                buffer_append_bytes(buffer, buf_size, pos, "\\t", 2);
                break;
            default:
                if (*p < 0x20) {
                    snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
                    buffer_append_bytes(buffer, buf_size, pos,
                                        escaped, strlen(escaped));
                } else {
                    buffer_append_bytes(buffer, buf_size, pos,
                                        (const char *)p, 1);
                }
                break;
        }
        p++;
    }

    buffer_append_bytes(buffer, buf_size, pos, "\"", 1);
}

static void resolve_exec_username(const client_t *client, char *buffer,
                                  size_t buf_size) {
    if (!buffer || buf_size == 0) {
        return;
    }

    if (client && client->ssh_login[0] != '\0' &&
        is_valid_username(client->ssh_login)) {
        snprintf(buffer, buf_size, "%s", client->ssh_login);
    } else {
        snprintf(buffer, buf_size, "%s", "anonymous");
    }

    if (utf8_strlen(buffer) > 20) {
        utf8_truncate(buffer, 20);
    }
}

static int exec_command_help(client_t *client) {
    static const char help_text[] =
        "TNT exec interface\n"
        "Commands:\n"
        "  help            Show this help\n"
        "  health          Print service health\n"
        "  users [--json]  List online users\n"
        "  stats [--json]  Print room statistics\n"
        "  tail [N]        Print recent messages\n"
        "  tail -n N       Print recent messages\n"
        "  post MESSAGE    Post a message non-interactively\n"
        "  post \"/me act\"  Post an action message\n"
        "  support         Show quick support guide\n"
        "  exit            Exit successfully\n";

    return client_send(client, help_text, sizeof(help_text) - 1) == 0 ? 0 : 1;
}

static int exec_command_support(client_t *client) {
    char output[2048] = {0};
    size_t pos = 0;

    support_append_exec_panel(output, sizeof(output), &pos);
    return client_send(client, output, pos) == 0 ? 0 : 1;
}

static int exec_command_health(client_t *client) {
    static const char ok[] = "ok\n";
    return client_send(client, ok, sizeof(ok) - 1) == 0 ? 0 : 1;
}

static int exec_command_users(client_t *client, bool json) {
    int count;
    char (*usernames)[MAX_USERNAME_LEN] = NULL;
    char *output;
    size_t output_size;
    size_t pos = 0;
    int rc;

    pthread_rwlock_rdlock(&g_room->lock);
    count = g_room->client_count;
    if (count > 0) {
        usernames = calloc((size_t)count, sizeof(*usernames));
        if (!usernames) {
            pthread_rwlock_unlock(&g_room->lock);
            client_printf(client, "users: out of memory\n");
            return 1;
        }

        for (int i = 0; i < count; i++) {
            snprintf(usernames[i], MAX_USERNAME_LEN, "%s",
                     g_room->clients[i]->username);
        }
    }
    pthread_rwlock_unlock(&g_room->lock);

    output_size = json ? ((size_t)count * (MAX_USERNAME_LEN * 2 + 8) + 8)
                       : ((size_t)count * (MAX_USERNAME_LEN + 1) + 1);
    if (output_size < 8) {
        output_size = 8;
    }

    output = calloc(output_size, 1);
    if (!output) {
        free(usernames);
        client_printf(client, "users: out of memory\n");
        return 1;
    }

    if (json) {
        buffer_append_bytes(output, output_size, &pos, "[", 1);
        for (int i = 0; i < count; i++) {
            if (i > 0) {
                buffer_append_bytes(output, output_size, &pos, ",", 1);
            }
            json_append_string(output, output_size, &pos, usernames[i]);
        }
        buffer_append_bytes(output, output_size, &pos, "]\n", 2);
    } else {
        for (int i = 0; i < count; i++) {
            buffer_appendf(output, output_size, &pos, "%s\n", usernames[i]);
        }
    }

    rc = client_send(client, output, pos) == 0 ? 0 : 1;
    free(output);
    free(usernames);
    return rc;
}

static int exec_command_stats(client_t *client, bool json) {
    int online_users;
    int message_count;
    int client_capacity;
    int active_connections;
    time_t now = time(NULL);
    long uptime_seconds;
    char buffer[512];
    int len;

    pthread_rwlock_rdlock(&g_room->lock);
    online_users = g_room->client_count;
    message_count = g_room->message_count;
    client_capacity = g_room->client_capacity;
    pthread_rwlock_unlock(&g_room->lock);

    active_connections = ratelimit_get_active_total();

    time_t start = ssh_server_start_time();
    uptime_seconds = (start > 0 && now >= start) ? (long)(now - start) : 0;

    if (json) {
        len = snprintf(buffer, sizeof(buffer),
                       "{\"status\":\"ok\",\"online_users\":%d,"
                       "\"message_count\":%d,\"client_capacity\":%d,"
                       "\"active_connections\":%d,\"uptime_seconds\":%ld}\n",
                       online_users, message_count, client_capacity,
                       active_connections, uptime_seconds);
    } else {
        len = snprintf(buffer, sizeof(buffer),
                       "status ok\n"
                       "online_users %d\n"
                       "message_count %d\n"
                       "client_capacity %d\n"
                       "active_connections %d\n"
                       "uptime_seconds %ld\n",
                       online_users, message_count, client_capacity,
                       active_connections, uptime_seconds);
    }

    if (len < 0 || len >= (int)sizeof(buffer)) {
        client_printf(client, "stats: output overflow\n");
        return 1;
    }

    return client_send(client, buffer, (size_t)len) == 0 ? 0 : 1;
}

static int parse_tail_count(const char *args, int *count) {
    char *end = NULL;
    long value;

    if (!count) {
        return -1;
    }

    *count = 20;
    if (!args || args[0] == '\0') {
        return 0;
    }

    if (strncmp(args, "-n", 2) == 0) {
        args += 2;
        while (*args && isspace((unsigned char)*args)) {
            args++;
        }
    }

    value = strtol(args, &end, 10);
    if (end == args) {
        return -1;
    }
    while (*end) {
        if (!isspace((unsigned char)*end)) {
            return -1;
        }
        end++;
    }

    if (value < 1 || value > MAX_MESSAGES) {
        return -1;
    }

    *count = (int)value;
    return 0;
}

static int exec_command_tail(client_t *client, const char *args) {
    int requested = 20;
    int total_messages;
    int start;
    int count;
    message_t *snapshot = NULL;
    char *output;
    size_t output_size;
    size_t pos = 0;
    int rc;

    if (parse_tail_count(args, &requested) < 0) {
        client_printf(client, "tail: usage: tail [N] | tail -n N\n");
        return 64;
    }

    pthread_rwlock_rdlock(&g_room->lock);
    total_messages = g_room->message_count;
    start = total_messages - requested;
    if (start < 0) {
        start = 0;
    }
    count = total_messages - start;

    if (count > 0) {
        snapshot = calloc((size_t)count, sizeof(message_t));
        if (!snapshot) {
            pthread_rwlock_unlock(&g_room->lock);
            client_printf(client, "tail: out of memory\n");
            return 1;
        }
        memcpy(snapshot, &g_room->messages[start], (size_t)count * sizeof(message_t));
    }
    pthread_rwlock_unlock(&g_room->lock);

    output_size = (size_t)(count > 0 ? count : 1) *
                  (MAX_USERNAME_LEN + MAX_MESSAGE_LEN + 48);
    output = calloc(output_size, 1);
    if (!output) {
        free(snapshot);
        client_printf(client, "tail: out of memory\n");
        return 1;
    }

    for (int i = 0; i < count; i++) {
        char timestamp[64];
        format_timestamp_utc(snapshot[i].timestamp, timestamp, sizeof(timestamp));
        buffer_appendf(output, output_size, &pos, "%s\t%s\t%s\n",
                       timestamp, snapshot[i].username, snapshot[i].content);
    }

    rc = client_send(client, output, pos) == 0 ? 0 : 1;
    free(output);
    free(snapshot);
    return rc;
}

static int exec_command_post(client_t *client, const char *args) {
    char content[MAX_MESSAGE_LEN];
    char username[MAX_USERNAME_LEN];
    message_t msg = {
        .timestamp = time(NULL),
    };

    if (!args || args[0] == '\0') {
        client_printf(client, "post: usage: post MESSAGE\n");
        return 64;
    }

    strncpy(content, args, sizeof(content) - 1);
    content[sizeof(content) - 1] = '\0';
    trim_ascii_whitespace(content);

    if (content[0] == '\0') {
        client_printf(client, "post: message cannot be empty\n");
        return 64;
    }

    if (!utf8_is_valid_string(content)) {
        client_printf(client, "post: invalid UTF-8 input\n");
        return 1;
    }

    resolve_exec_username(client, username, sizeof(username));

    if (strncmp(content, "/me ", 4) == 0 && content[4] != '\0') {
        msg.username[0] = '*';
        msg.username[1] = '\0';
        int n = snprintf(msg.content, sizeof(msg.content), "%s %s", username, content + 4);
        if (n >= (int)sizeof(msg.content)) {
            msg.content[sizeof(msg.content) - 1] = '\0';
        }
    } else {
        strncpy(msg.username, username, sizeof(msg.username) - 1);
        msg.username[sizeof(msg.username) - 1] = '\0';
        strncpy(msg.content, content, sizeof(msg.content) - 1);
        msg.content[sizeof(msg.content) - 1] = '\0';
    }

    room_broadcast(g_room, &msg);
    if (client_send(client, "posted\n", 7) != 0) {
        return 1;
    }

    notify_mentions(msg.content, client);
    if (message_save(&msg) < 0) {
        fprintf(stderr, "post: failed to persist message\n");
        return 1;
    }

    return 0;
}

int exec_dispatch(client_t *client) {
    char command_copy[MAX_EXEC_COMMAND_LEN];
    char *cmd;
    char *args;

    strncpy(command_copy, client->exec_command, sizeof(command_copy) - 1);
    command_copy[sizeof(command_copy) - 1] = '\0';
    trim_ascii_whitespace(command_copy);

    cmd = command_copy;
    if (*cmd == '\0') {
        return exec_command_help(client);
    }

    args = cmd;
    while (*args && !isspace((unsigned char)*args)) {
        args++;
    }
    if (*args) {
        *args++ = '\0';
        while (*args && isspace((unsigned char)*args)) {
            args++;
        }
    } else {
        args = NULL;
    }

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0) {
        return exec_command_help(client);
    }
    if (strcmp(cmd, "support") == 0 || strcmp(cmd, "guide") == 0) {
        return exec_command_support(client);
    }
    if (strcmp(cmd, "health") == 0) {
        return exec_command_health(client);
    }
    if (strcmp(cmd, "users") == 0) {
        if (args && strcmp(args, "--json") != 0) {
            client_printf(client, "users: usage: users [--json]\n");
            return 64;
        }
        return exec_command_users(client, args != NULL);
    }
    if (strcmp(cmd, "stats") == 0) {
        if (args && strcmp(args, "--json") != 0) {
            client_printf(client, "stats: usage: stats [--json]\n");
            return 64;
        }
        return exec_command_stats(client, args != NULL);
    }
    if (strcmp(cmd, "tail") == 0) {
        return exec_command_tail(client, args);
    }
    if (strcmp(cmd, "post") == 0) {
        return exec_command_post(client, args);
    }
    if (strcmp(cmd, "exit") == 0) {
        return 0;
    }

    client_printf(client, "Unknown command: %s\n", cmd);
    return 64;
}
