#include "module_protocol.h"

#include "common.h"
#include "json_text.h"
#include "message_log.h"
#include "utf8.h"

static bool append_was_truncated(size_t pos, size_t buf_size) {
    return buf_size == 0 || pos >= buf_size - 1;
}

static bool has_plain_text_controls(const char *text) {
    const unsigned char *p = (const unsigned char *)text;

    while (p && *p) {
        if (*p < 32 || *p == 127) {
            return true;
        }
        p++;
    }

    return false;
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

    return append_was_truncated(*pos, buf_size) && *pos == buf_size - 1 &&
                   before != *pos
               ? -1
               : 0;
}

int tnt_module_append_message_created(char *buffer, size_t buf_size,
                                      size_t *pos, const char *message_id,
                                      const message_t *msg) {
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
    buffer_appendf(buffer, buf_size, pos, ",\"metadata\":{}}}\n");

    return append_was_truncated(*pos, buf_size) && *pos == buf_size - 1 &&
                   before != *pos
               ? -1
               : 0;
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
                                   sizeof(plain_text))) {
        return false;
    }

    if (plain_text[0] == '\0' ||
        strlen(plain_text) >= sizeof(out->plain_text) ||
        !utf8_is_valid_string(plain_text) ||
        has_plain_text_controls(plain_text)) {
        return false;
    }

    snprintf(out->plain_text, sizeof(out->plain_text), "%s", plain_text);
    return true;
}
