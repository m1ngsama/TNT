#include "exec.h"
#include "chat_room.h"
#include "client.h"
#include "common.h"
#include "exec_catalog.h"
#include "i18n.h"
#include "input.h"
#include "json_text.h"
#include "message.h"
#include "module_runtime.h"
#include "ratelimit.h"
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
    char help_text[1024];
    size_t pos = 0;

    help_text[0] = '\0';
    exec_catalog_append_help(help_text, sizeof(help_text), &pos,
                             client->ui_lang);
    return client_send(client, help_text, pos) == 0 ? TNT_EXIT_OK
                                                   : TNT_EXIT_ERROR;
}

static int exec_command_usage(client_t *client, tnt_exec_command_id_t id) {
    char usage[128];
    size_t pos = 0;

    usage[0] = '\0';
    exec_catalog_append_usage(usage, sizeof(usage), &pos, id,
                              client->ui_lang);
    client_printf(client, "%s", usage);
    return TNT_EXIT_USAGE;
}

static int exec_command_health(client_t *client) {
    static const char ok[] = "ok\n";
    return client_send(client, ok, sizeof(ok) - 1) == 0 ? TNT_EXIT_OK
                                                       : TNT_EXIT_ERROR;
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
            return TNT_EXIT_ERROR;
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
        return TNT_EXIT_ERROR;
    }

    if (json) {
        buffer_append_bytes(output, output_size, &pos, "[", 1);
        for (int i = 0; i < count; i++) {
            if (i > 0) {
                buffer_append_bytes(output, output_size, &pos, ",", 1);
            }
            tnt_json_append_string(output, output_size, &pos, usernames[i]);
        }
        buffer_append_bytes(output, output_size, &pos, "]\n", 2);
    } else {
        for (int i = 0; i < count; i++) {
            buffer_appendf(output, output_size, &pos, "%s\n", usernames[i]);
        }
    }

    rc = client_send(client, output, pos) == 0 ? TNT_EXIT_OK : TNT_EXIT_ERROR;
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
        return TNT_EXIT_ERROR;
    }

    return client_send(client, buffer, (size_t)len) == 0 ? TNT_EXIT_OK
                                                        : TNT_EXIT_ERROR;
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

static int parse_dump_count(const char *args, int *count) {
    char *end = NULL;
    long value;

    if (!count) {
        return -1;
    }

    *count = 0;
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

    if (value < 1 || value > 10000) {
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
        return exec_command_usage(client, TNT_EXEC_COMMAND_TAIL);
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
            return TNT_EXIT_ERROR;
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
        return TNT_EXIT_ERROR;
    }

    for (int i = 0; i < count; i++) {
        char timestamp[64];
        format_timestamp_utc(snapshot[i].timestamp, timestamp, sizeof(timestamp));
        buffer_appendf(output, output_size, &pos, "%s\t%s\t%s\n",
                       timestamp, snapshot[i].username, snapshot[i].content);
    }

    rc = client_send(client, output, pos) == 0 ? TNT_EXIT_OK : TNT_EXIT_ERROR;
    free(output);
    free(snapshot);
    return rc;
}

static int exec_command_dump(client_t *client, const char *args) {
    int requested = 0;
    char *output = NULL;
    size_t output_len = 0;
    int rc;

    if (parse_dump_count(args, &requested) < 0) {
        return exec_command_usage(client, TNT_EXEC_COMMAND_DUMP);
    }

    if (message_dump_text(&output, &output_len, requested) < 0) {
        client_printf(client, "dump: failed to read message log\n");
        return TNT_EXIT_ERROR;
    }

    rc = client_send(client, output, output_len) == 0 ? TNT_EXIT_OK
                                                     : TNT_EXIT_ERROR;
    free(output);
    return rc;
}

static int exec_command_post(client_t *client, const char *args) {
    char content[MAX_MESSAGE_LEN];
    char username[MAX_USERNAME_LEN];
    message_t msg = {
        .timestamp = time(NULL),
    };

    if (!args || args[0] == '\0') {
        return exec_command_usage(client, TNT_EXEC_COMMAND_POST);
    }

    if (strlen(args) >= sizeof(content)) {
        client_printf(client, "%s",
                      i18n_text(client->ui_lang, I18N_EXEC_POST_TOO_LONG));
        return TNT_EXIT_USAGE;
    }

    strncpy(content, args, sizeof(content) - 1);
    content[sizeof(content) - 1] = '\0';
    trim_ascii_whitespace(content);

    if (content[0] == '\0') {
        client_printf(client, "%s",
                      i18n_text(client->ui_lang, I18N_EXEC_POST_EMPTY));
        return TNT_EXIT_USAGE;
    }

    if (!utf8_is_valid_string(content)) {
        client_printf(client, "%s",
                      i18n_text(client->ui_lang,
                                I18N_EXEC_POST_INVALID_UTF8));
        return TNT_EXIT_ERROR;
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

    if (message_save(&msg) < 0) {
        fprintf(stderr, "post: failed to persist message\n");
        client_printf(client, "%s",
                      i18n_text(client->ui_lang,
                                I18N_EXEC_POST_PERSIST_FAILED));
        return TNT_EXIT_ERROR;
    }

    room_broadcast(g_room, &msg);
    notify_mentions(msg.content, client);
    tnt_module_runtime_publish_message_created(&msg);

    if (client_send(client, "posted\n", 7) != 0) {
        return TNT_EXIT_ERROR;
    }

    return TNT_EXIT_OK;
}

int exec_dispatch(client_t *client) {
    char command_copy[MAX_EXEC_COMMAND_LEN];
    tnt_exec_command_id_t command_id;
    const char *args = NULL;

    if (client->exec_command_too_long) {
        client_printf(client, "%s",
                      i18n_text(client->ui_lang,
                                I18N_EXEC_COMMAND_TOO_LONG));
        return TNT_EXIT_USAGE;
    }

    strncpy(command_copy, client->exec_command, sizeof(command_copy) - 1);
    command_copy[sizeof(command_copy) - 1] = '\0';
    trim_ascii_whitespace(command_copy);

    if (command_copy[0] == '\0') {
        return exec_command_help(client);
    }

    if (exec_catalog_match(command_copy, &command_id, &args)) {
        if (!exec_catalog_args_valid(command_id, args)) {
            return exec_command_usage(client, command_id);
        }

        switch (command_id) {
            case TNT_EXEC_COMMAND_HELP:
                return exec_command_help(client);
            case TNT_EXEC_COMMAND_HEALTH:
                return exec_command_health(client);
            case TNT_EXEC_COMMAND_USERS:
                return exec_command_users(client, args != NULL);
            case TNT_EXEC_COMMAND_STATS:
                return exec_command_stats(client, args != NULL);
            case TNT_EXEC_COMMAND_TAIL:
                return exec_command_tail(client, args);
            case TNT_EXEC_COMMAND_DUMP:
                return exec_command_dump(client, args);
            case TNT_EXEC_COMMAND_POST:
                return exec_command_post(client, args);
            case TNT_EXEC_COMMAND_EXIT:
                return TNT_EXIT_OK;
            case TNT_EXEC_COMMAND_COUNT:
                break;
        }
    }

    for (char *p = command_copy; *p; p++) {
        if (isspace((unsigned char)*p)) {
            *p = '\0';
            break;
        }
    }
    client_printf(client,
                  i18n_text(client->ui_lang,
                            I18N_EXEC_UNKNOWN_COMMAND_FORMAT),
                  command_copy);
    return TNT_EXIT_USAGE;
}
