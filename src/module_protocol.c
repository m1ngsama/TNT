#include "module_protocol.h"

#include "common.h"
#include "json_text.h"
#include "message_log.h"
#include "utf8.h"

#include <strings.h>

#define TNT_MODULE_SENDER_MAX_CHARS 20

static int append_result(size_t before, size_t pos, size_t buf_size) {
    return pos == buf_size - 1 && pos != before ? -1 : 0;
}

static bool is_valid_plain_text(const char *plain_text) {
    return plain_text[0] != '\0' && strlen(plain_text) < MAX_MESSAGE_LEN &&
           utf8_is_valid_string(plain_text) &&
           !message_log_content_has_forbidden_control(plain_text);
}

static bool is_valid_nickname(const char *nickname) {
    return utf8_is_valid_string(nickname) && is_valid_username(nickname) &&
           strchr(nickname, ':') == NULL &&
           utf8_strlen(nickname) <= TNT_MODULE_SENDER_MAX_CHARS;
}

int tnt_module_append_handshake(char *buffer, size_t buf_size, size_t *pos,
                                const char *server_version) {
    const char *version = server_version ? server_version : TNT_VERSION;
    size_t before;

    if (!buffer || !pos || buf_size == 0) {
        return -1;
    }

    before = *pos;
    buffer_appendf(buffer, buf_size, pos,
                   "{\"type\":\"handshake\",\"protocol\":");
    tnt_json_append_string(buffer, buf_size, pos, TNT_MODULE_PROTOCOL_VERSION);
    buffer_appendf(buffer, buf_size, pos,
                   ",\"server\":{\"name\":\"tnt\",\"version\":");
    tnt_json_append_string(buffer, buf_size, pos, version);
    buffer_appendf(buffer, buf_size, pos, "}}\n");

    return append_result(before, *pos, buf_size);
}

int tnt_module_append_message_created(char *buffer, size_t buf_size,
                                      size_t *pos, const char *message_id,
                                      const message_t *msg, bool verified) {
    char timestamp[64];
    size_t before;

    if (!buffer || !pos || buf_size == 0 || !message_id || !msg ||
        message_id[0] == '\0' || !utf8_is_valid_string(msg->username) ||
        !utf8_is_valid_string(msg->content)) {
        return -1;
    }

    before = *pos;
    message_log_format_timestamp_utc(msg->timestamp, timestamp,
                                     sizeof(timestamp));
    buffer_appendf(buffer, buf_size, pos,
                   "{\"type\":\"%s\",\"message\":{\"id\":",
                   TNT_MODULE_EVENT_MESSAGE_CREATED);
    tnt_json_append_string(buffer, buf_size, pos, message_id);
    buffer_appendf(buffer, buf_size, pos, ",\"timestamp\":");
    tnt_json_append_string(buffer, buf_size, pos, timestamp);
    buffer_appendf(buffer, buf_size, pos, ",\"sender\":");
    tnt_json_append_string(buffer, buf_size, pos, msg->username);
    buffer_appendf(buffer, buf_size, pos, ",\"kind\":\"text\","
                   "\"plain_text\":");
    tnt_json_append_string(buffer, buf_size, pos, msg->content);
    buffer_appendf(buffer, buf_size, pos, ",\"metadata\":{%s}}}\n",
                   verified ? "\"verified\":true" : "");

    return append_result(before, *pos, buf_size);
}

int tnt_module_append_presence(char *buffer, size_t buf_size, size_t *pos,
                               const char *type, const char *nickname,
                               time_t timestamp) {
    char formatted[64];
    size_t before;

    if (!buffer || !pos || buf_size == 0 || !type || !nickname ||
        nickname[0] == '\0' || !utf8_is_valid_string(nickname)) {
        return -1;
    }

    before = *pos;
    message_log_format_timestamp_utc(timestamp, formatted, sizeof(formatted));
    buffer_appendf(buffer, buf_size, pos, "{\"type\":");
    tnt_json_append_string(buffer, buf_size, pos, type);
    buffer_appendf(buffer, buf_size, pos, ",\"nickname\":");
    tnt_json_append_string(buffer, buf_size, pos, nickname);
    buffer_appendf(buffer, buf_size, pos, ",\"timestamp\":");
    tnt_json_append_string(buffer, buf_size, pos, formatted);
    buffer_appendf(buffer, buf_size, pos, "}\n");

    return append_result(before, *pos, buf_size);
}

int tnt_module_append_presence_snapshot(char *buffer, size_t buf_size,
                                        size_t *pos,
                                        const char *const *nicknames,
                                        size_t count) {
    size_t before;

    if (!buffer || !pos || buf_size == 0 || (!nicknames && count > 0)) {
        return -1;
    }

    before = *pos;
    buffer_appendf(buffer, buf_size, pos, "{\"type\":\"%s\",\"nicknames\":[",
                   TNT_MODULE_EVENT_PRESENCE_SNAPSHOT);
    for (size_t i = 0; i < count; i++) {
        if (i > 0) {
            buffer_append_bytes(buffer, buf_size, pos, ",", 1);
        }
        tnt_json_append_string(buffer, buf_size, pos, nicknames[i]);
    }
    buffer_appendf(buffer, buf_size, pos, "]}\n");

    return append_result(before, *pos, buf_size);
}

int tnt_module_append_identity_requested(char *buffer, size_t buf_size,
                                         size_t *pos, const char *session,
                                         const char *nickname) {
    size_t before;

    if (!buffer || !pos || buf_size == 0 || !session || session[0] == '\0' ||
        !nickname || !utf8_is_valid_string(nickname)) {
        return -1;
    }

    before = *pos;
    buffer_appendf(buffer, buf_size, pos, "{\"type\":\"%s\",\"session\":",
                   TNT_MODULE_EVENT_IDENTITY_REQUESTED);
    tnt_json_append_string(buffer, buf_size, pos, session);
    buffer_appendf(buffer, buf_size, pos, ",\"nickname\":");
    tnt_json_append_string(buffer, buf_size, pos, nickname);
    buffer_appendf(buffer, buf_size, pos, "}\n");

    return append_result(before, *pos, buf_size);
}

int tnt_module_append_private_created(char *buffer, size_t buf_size,
                                      size_t *pos, const char *from,
                                      const char *to, const char *plain_text,
                                      time_t timestamp, bool verified) {
    char formatted[64];
    size_t before;

    if (!buffer || !pos || buf_size == 0 || !from || !to || !plain_text ||
        !utf8_is_valid_string(from) || !utf8_is_valid_string(to) ||
        !utf8_is_valid_string(plain_text)) {
        return -1;
    }

    before = *pos;
    message_log_format_timestamp_utc(timestamp, formatted, sizeof(formatted));
    buffer_appendf(buffer, buf_size, pos, "{\"type\":\"%s\",\"from\":",
                   TNT_MODULE_EVENT_PRIVATE_CREATED);
    tnt_json_append_string(buffer, buf_size, pos, from);
    buffer_appendf(buffer, buf_size, pos, ",\"to\":");
    tnt_json_append_string(buffer, buf_size, pos, to);
    buffer_appendf(buffer, buf_size, pos, ",\"plain_text\":");
    tnt_json_append_string(buffer, buf_size, pos, plain_text);
    buffer_appendf(buffer, buf_size, pos, ",\"timestamp\":");
    tnt_json_append_string(buffer, buf_size, pos, formatted);
    buffer_appendf(buffer, buf_size, pos, ",\"verified\":%s}\n",
                   verified ? "true" : "false");

    return append_result(before, *pos, buf_size);
}

bool tnt_module_parse_message_create(const char *line,
                                     tnt_module_message_create_t *out) {
    char type[64];
    char plain_text[MAX_MESSAGE_LEN];

    if (!line || !out) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    if (!tnt_json_get_string_field(line, "type", type, sizeof(type)) ||
        strcmp(type, TNT_MODULE_RESPONSE_MESSAGE_CREATE) != 0) {
        return false;
    }

    if (!tnt_json_get_string_field(line, "plain_text", plain_text,
                                   sizeof(plain_text)) ||
        !is_valid_plain_text(plain_text)) {
        return false;
    }

    snprintf(out->plain_text, sizeof(out->plain_text), "%s", plain_text);
    return true;
}

bool tnt_module_parse_message_post(const char *line,
                                   tnt_module_message_post_t *out) {
    char type[64];

    if (!line || !out) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    return tnt_json_get_string_field(line, "type", type, sizeof(type)) &&
           strcmp(type, TNT_MODULE_RECORD_MESSAGE_POST) == 0 &&
           tnt_json_get_string_field(line, "sender", out->sender,
                                     sizeof(out->sender)) &&
           is_valid_nickname(out->sender) &&
           tnt_json_get_string_field(line, "plain_text", out->plain_text,
                                     sizeof(out->plain_text)) &&
           is_valid_plain_text(out->plain_text);
}

bool tnt_module_parse_identity_record(const char *line,
                                      tnt_module_identity_record_t *out) {
    char type[64];

    if (!line || !out) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    if (!tnt_json_get_string_field(line, "type", type, sizeof(type))) {
        return false;
    }
    if (strcmp(type, "identity.notice") == 0) {
        out->kind = TNT_MODULE_IDENTITY_NOTICE;
        return tnt_json_get_string_field(line, "session", out->session,
                                         sizeof(out->session)) &&
               out->session[0] != '\0' &&
               tnt_json_get_string_field(line, "plain_text", out->plain_text,
                                         sizeof(out->plain_text)) &&
               is_valid_plain_text(out->plain_text);
    }
    if (strcmp(type, "identity.verified") == 0) {
        out->kind = TNT_MODULE_IDENTITY_VERIFIED;
        if (!tnt_json_get_string_field(line, "session", out->session,
                                       sizeof(out->session)) ||
            out->session[0] == '\0') {
            return false;
        }
    } else if (strcmp(type, "identity.reserve") == 0) {
        out->kind = TNT_MODULE_IDENTITY_RESERVE;
    } else {
        return false;
    }
    return tnt_json_get_string_field(line, "handle", out->handle,
                                     sizeof(out->handle)) &&
           identity_handle_valid(out->handle);
}

bool tnt_module_parse_presence_remote(const char *line,
                                      tnt_module_presence_remote_t *out) {
    char type[64];
    size_t count;

    if (!line || !out) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    if (!tnt_json_get_string_field(line, "type", type, sizeof(type)) ||
        strcmp(type, TNT_MODULE_RECORD_PRESENCE_REMOTE) != 0 ||
        !tnt_json_get_string_array_field(line, "nicknames",
                                         &out->nicknames[0][0],
                                         sizeof(out->nicknames[0]),
                                         TNT_MODULE_REMOTE_USERS_MAX,
                                         &count)) {
        memset(out, 0, sizeof(*out));
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        bool repeated = false;

        if (!is_valid_nickname(out->nicknames[i])) {
            memset(out, 0, sizeof(*out));
            return false;
        }
        for (size_t j = 0; j < out->count && !repeated; j++) {
            repeated = strcasecmp(out->nicknames[j], out->nicknames[i]) == 0;
        }
        if (!repeated) {
            memmove(out->nicknames[out->count], out->nicknames[i],
                    sizeof(out->nicknames[0]));
            out->count++;
        }
    }
    return true;
}

bool tnt_module_parse_private_post(const char *line,
                                   tnt_module_private_post_t *out) {
    char type[64];

    if (!line || !out) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    return tnt_json_get_string_field(line, "type", type, sizeof(type)) &&
           strcmp(type, TNT_MODULE_RECORD_PRIVATE_POST) == 0 &&
           tnt_json_get_string_field(line, "from", out->from,
                                     sizeof(out->from)) &&
           is_valid_nickname(out->from) &&
           tnt_json_get_string_field(line, "to", out->to, sizeof(out->to)) &&
           is_valid_nickname(out->to) &&
           tnt_json_get_string_field(line, "plain_text", out->plain_text,
                                     sizeof(out->plain_text)) &&
           is_valid_plain_text(out->plain_text);
}
