#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE  /* for timegm() on glibc */
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE /* for timegm() on macOS */
#endif

#include "message_log.h"
#include "utf8.h"

static time_t parse_rfc3339_utc(const char *timestamp_str) {
    struct tm tm = {0};

    if (!timestamp_str) {
        return (time_t)-1;
    }

    char *result = strptime(timestamp_str, "%Y-%m-%dT%H:%M:%SZ", &tm);
    if (!result || *result != '\0') {
        return (time_t)-1;
    }

    return timegm(&tm);
}

void message_log_format_timestamp_utc(time_t ts, char *buffer,
                                      size_t buf_size) {
    struct tm tm_info;

    if (!buffer || buf_size == 0) {
        return;
    }

    gmtime_r(&ts, &tm_info);
    strftime(buffer, buf_size, "%Y-%m-%dT%H:%M:%SZ", &tm_info);
}

bool message_log_parse_record(const char *line, message_t *out, time_t now) {
    char line_copy[MESSAGE_LOG_MAX_LINE];
    char *first_sep;
    char *second_sep;
    char *timestamp_str;
    char *username;
    char *content;
    time_t msg_time;
    size_t line_len;

    if (!line || !out) {
        return false;
    }

    line_len = strlen(line);
    if (line_len == 0 || line[line_len - 1] != '\n') {
        return false;
    }
    if (line_len >= sizeof(line_copy)) {
        return false;
    }

    memcpy(line_copy, line, line_len + 1);
    line_copy[line_len - 1] = '\0';

    first_sep = strchr(line_copy, '|');
    if (!first_sep) {
        return false;
    }
    second_sep = strchr(first_sep + 1, '|');
    if (!second_sep || strchr(second_sep + 1, '|')) {
        return false;
    }

    *first_sep = '\0';
    *second_sep = '\0';
    timestamp_str = line_copy;
    username = first_sep + 1;
    content = second_sep + 1;

    if (timestamp_str[0] == '\0' || username[0] == '\0' ||
        content[0] == '\0') {
        return false;
    }
    if (strlen(username) >= MAX_USERNAME_LEN ||
        strlen(content) >= MAX_MESSAGE_LEN) {
        return false;
    }
    if (!utf8_is_valid_string(username) || !utf8_is_valid_string(content)) {
        return false;
    }

    msg_time = parse_rfc3339_utc(timestamp_str);
    if (msg_time == (time_t)-1) {
        return false;
    }
    if (msg_time > now + 86400 || msg_time < now - 31536000 * 10) {
        return false;
    }

    out->timestamp = msg_time;
    strncpy(out->username, username, MAX_USERNAME_LEN - 1);
    out->username[MAX_USERNAME_LEN - 1] = '\0';
    strncpy(out->content, content, MAX_MESSAGE_LEN - 1);
    out->content[MAX_MESSAGE_LEN - 1] = '\0';
    return true;
}

int message_log_format_record(const message_t *msg, char *buffer,
                              size_t buf_size, size_t *record_len) {
    char timestamp[64];
    int needed;

    if (!msg) {
        return -1;
    }

    message_log_format_timestamp_utc(msg->timestamp, timestamp,
                                     sizeof(timestamp));
    needed = snprintf(buffer, buf_size, "%s|%s|%s\n", timestamp,
                      msg->username, msg->content);
    if (needed < 0) {
        return -1;
    }
    if (record_len) {
        *record_len = (size_t)needed;
    }
    if (!buffer || buf_size == 0) {
        return 0;
    }
    return (size_t)needed < buf_size ? 0 : -1;
}
