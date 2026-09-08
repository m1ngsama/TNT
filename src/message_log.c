#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE  /* for timegm() on glibc */
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE /* for timegm() on macOS */
#endif

#include "message_log.h"
#include "utf8.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

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

bool message_log_is_header(const char *line) {
    size_t len;

    if (!line) {
        return false;
    }
    len = strlen(MESSAGE_LOG_HEADER);
    if (strncmp(line, MESSAGE_LOG_HEADER, len) != 0) {
        return false;
    }
    /* Accept the line with or without its terminator. */
    return line[len] == '\0' || line[len] == '\n';
}

bool message_log_encode_content(const char *in, char *out, size_t out_size) {
    size_t pos = 0;

    if (!in || !out || out_size == 0) {
        return false;
    }

    for (; *in; in++) {
        const char *escape = NULL;

        if (*in == '\\') {
            escape = "\\\\";
        } else if (*in == '\n') {
            escape = "\\n";
        }

        if (escape) {
            if (pos + 2 >= out_size) {
                return false;
            }
            out[pos++] = escape[0];
            out[pos++] = escape[1];
        } else {
            if (pos + 1 >= out_size) {
                return false;
            }
            out[pos++] = *in;
        }
    }

    out[pos] = '\0';
    return true;
}

bool message_log_decode_content(const char *in, char *out, size_t out_size) {
    size_t pos = 0;

    if (!in || !out || out_size == 0) {
        return false;
    }

    while (*in) {
        char decoded;

        if (*in != '\\') {
            decoded = *in++;
        } else {
            in++;
            switch (*in) {
            case '\\': decoded = '\\'; break;
            case 'n':  decoded = '\n'; break;
            default:
                /* Not something the encoder can produce.  Refusing keeps a
                 * corrupted record out of the room rather than guessing. */
                return false;
            }
            in++;
        }

        if (pos + 1 >= out_size) {
            return false;
        }
        out[pos++] = decoded;
    }

    out[pos] = '\0';
    return true;
}

/* Content may hold a newline; nothing else below 0x20, no DEL, no C1.  That
 * single exception is what keeps a user from sending escape sequences to
 * every terminal in the room. */
static bool content_has_forbidden_control(const char *s) {
    char stripped[MAX_MESSAGE_LEN * 2];
    size_t pos = 0;

    for (; *s && pos + 1 < sizeof(stripped); s++) {
        if (*s == '\n') {
            continue;
        }
        stripped[pos++] = *s;
    }
    stripped[pos] = '\0';
    return utf8_contains_control(stripped);
}

/* Rewrite one v1 record with its content field escaped.  A line that is not a
 * record is copied through: the parser already skips it, and the backup is the
 * authority if it ever mattered.  False means the record cannot be represented
 * in v2 and is dropped rather than written back decoding to something else. */
static bool migrate_line(const char *line, char *out, size_t out_size) {
    const char *first_sep;
    const char *second_sep;
    const char *content_start;
    char content[MESSAGE_LOG_MAX_LINE];
    char encoded[MESSAGE_LOG_MAX_LINE];
    size_t content_len;
    int written;

    first_sep = strchr(line, '|');
    second_sep = first_sep ? strchr(first_sep + 1, '|') : NULL;
    if (!second_sep) {
        written = snprintf(out, out_size, "%s", line);
        return written >= 0 && (size_t)written < out_size;
    }

    content_start = second_sep + 1;
    content_len = strlen(content_start);
    while (content_len > 0 && (content_start[content_len - 1] == '\n' ||
                               content_start[content_len - 1] == '\r')) {
        content_len--;
    }
    if (content_len >= sizeof(content)) {
        return false;
    }
    memcpy(content, content_start, content_len);
    content[content_len] = '\0';

    if (!message_log_encode_content(content, encoded, sizeof(encoded))) {
        return false;
    }

    written = snprintf(out, out_size, "%.*s%s\n",
                       (int)(content_start - line), line, encoded);
    return written >= 0 && (size_t)written < out_size;
}

static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "r");
    FILE *out;
    char buf[8192];
    size_t n;
    int rc = 0;

    if (!in) {
        return -1;
    }
    out = fopen(dst, "w");
    if (!out) {
        fclose(in);
        return -1;
    }

    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            rc = -1;
            break;
        }
    }
    if (ferror(in) || fflush(out) != 0 || fsync(fileno(out)) != 0) {
        rc = -1;
    }
    fclose(in);
    if (fclose(out) != 0) {
        rc = -1;
    }
    if (rc < 0) {
        unlink(dst);
    }
    return rc;
}

/* Make a rename durable: without this the log can come back from a crash
 * pointing at the file the rename replaced. */
static void sync_parent_dir(const char *path) {
    char dir[PATH_MAX];
    const char *slash = strrchr(path, '/');
    int fd;

    if (!slash) {
        snprintf(dir, sizeof(dir), ".");
    } else {
        size_t len = slash == path ? 1 : (size_t)(slash - path);
        if (len >= sizeof(dir)) {
            return;
        }
        memcpy(dir, path, len);
        dir[len] = '\0';
    }

    fd = open(dir, O_RDONLY);
    if (fd < 0) {
        return;
    }
    fsync(fd);
    close(fd);
}

int message_log_migrate(const char *path) {
    char backup[PATH_MAX];
    char tmp[PATH_MAX];
    char line[MESSAGE_LOG_MAX_LINE];
    char migrated[MESSAGE_LOG_MAX_LINE];
    FILE *in;
    FILE *out;
    bool at_record_start = true;
    int rc = 0;

    if (!path || path[0] == '\0') {
        return -1;
    }
    if ((size_t)snprintf(backup, sizeof(backup), "%s.v1.bak", path) >=
            sizeof(backup) ||
        (size_t)snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= sizeof(tmp)) {
        return -1;
    }

    in = fopen(path, "r");
    if (!in) {
        return errno == ENOENT ? 0 : -1;
    }
    if (!fgets(line, sizeof(line), in) || message_log_is_header(line)) {
        /* Empty, or already v2.  message_save writes the header when it
         * creates the file, so a fresh install never migrates. */
        fclose(in);
        return 0;
    }
    rewind(in);

    if (copy_file(path, backup) < 0) {
        fclose(in);
        return -1;
    }

    out = fopen(tmp, "w");
    if (!out) {
        fclose(in);
        return -1;
    }
    if (fputs(MESSAGE_LOG_HEADER "\n", out) < 0) {
        rc = -1;
    }

    while (rc == 0 && fgets(line, sizeof(line), in)) {
        size_t len = strlen(line);
        bool complete = len > 0 && line[len - 1] == '\n';
        /* A line too long for the buffer arrives in pieces; only the first
         * piece is a record, the rest is copied byte for byte. */
        const char *emit = line;

        if (at_record_start && complete) {
            if (!migrate_line(line, migrated, sizeof(migrated))) {
                at_record_start = true;
                continue;
            }
            emit = migrated;
        }
        at_record_start = complete;

        if (fputs(emit, out) < 0) {
            rc = -1;
        }
    }
    if (ferror(in)) {
        rc = -1;
    }

    if (rc == 0 && (fflush(out) != 0 || fsync(fileno(out)) != 0)) {
        rc = -1;
    }
    fclose(in);
    if (fclose(out) != 0) {
        rc = -1;
    }

    if (rc < 0 || rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    sync_parent_dir(path);
    return 0;
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
    char decoded[MAX_MESSAGE_LEN];

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
    if (!utf8_is_valid_string(username) || !utf8_is_valid_string(content) ||
        utf8_contains_control(username) ||
        utf8_contains_control(content)) {
        return false;
    }

    /* The stored form is escaped; the room works with the decoded text. */
    if (!message_log_decode_content(content, decoded, sizeof(decoded))) {
        return false;
    }
    if (content_has_forbidden_control(decoded)) {
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
    strncpy(out->content, decoded, MAX_MESSAGE_LEN - 1);
    out->content[MAX_MESSAGE_LEN - 1] = '\0';
    out->display_time[0] = '\0';
    out->display_date[0] = '\0';
    return true;
}

int message_log_format_record(const message_t *msg, char *buffer,
                              size_t buf_size, size_t *record_len) {
    char timestamp[64];
    int needed;

    char encoded[MAX_MESSAGE_LEN];

    if (!msg || !utf8_is_valid_string(msg->username) ||
        !utf8_is_valid_string(msg->content) ||
        utf8_contains_control(msg->username) ||
        content_has_forbidden_control(msg->content)) {
        return -1;
    }

    /* The length limit applies to the stored form, so the parser contract is
     * unchanged: a newline costs two bytes on disk. */
    if (!message_log_encode_content(msg->content, encoded, sizeof(encoded))) {
        return -1;
    }

    message_log_format_timestamp_utc(msg->timestamp, timestamp,
                                     sizeof(timestamp));
    needed = snprintf(buffer, buf_size, "%s|%s|%s\n", timestamp,
                      msg->username, encoded);
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
