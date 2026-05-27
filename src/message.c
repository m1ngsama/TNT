#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE  /* for strcasestr() on glibc */
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE /* for strcasestr() on macOS */
#endif

#include "message.h"
#include "message_log.h"
#include "utf8.h"
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

static pthread_mutex_t g_message_file_lock = PTHREAD_MUTEX_INITIALIZER;

static void discard_line_remainder(FILE *fp) {
    int c;

    while ((c = fgetc(fp)) != '\n' && c != EOF) {
    }
}

static int append_dump_record(char **output, size_t *capacity,
                              size_t *len, const message_t *msg) {
    size_t needed;
    size_t available;

    if (!output || !capacity || !len || !msg) {
        return -1;
    }

    if (message_log_format_record(msg, NULL, 0, &needed) < 0) {
        return -1;
    }

    available = *capacity > *len ? *capacity - *len : 0;
    if (needed + 1 > available) {
        size_t new_capacity = *capacity ? *capacity : 1024;
        while (needed + 1 > new_capacity - *len) {
            if (new_capacity > SIZE_MAX / 2) {
                return -1;
            }
            new_capacity *= 2;
        }

        char *grown = realloc(*output, new_capacity);
        if (!grown) {
            return -1;
        }
        *output = grown;
        *capacity = new_capacity;
    }

    if (message_log_format_record(msg, *output + *len, *capacity - *len,
                                  NULL) < 0) {
        return -1;
    }
    *len += needed;
    return 0;
}

/* Initialize message subsystem */
void message_init(void) {
    /* Nothing to initialize for now */
}

/* Load messages from log file - Optimized for large files.
 * Holds g_message_file_lock for the duration of the read so concurrent
 * message_save() calls from chat threads cannot interleave a partial line. */
int message_load(message_t **messages, int max_messages) {
    char log_path[PATH_MAX];

    /* Always allocate the message array */
    message_t *msg_array = calloc(max_messages, sizeof(message_t));
    if (!msg_array) {
        return 0;
    }

    if (tnt_state_path(log_path, sizeof(log_path), LOG_FILE) < 0) {
        *messages = msg_array;
        return 0;
    }

    pthread_mutex_lock(&g_message_file_lock);

    FILE *fp = fopen(log_path, "r");
    if (!fp) {
        /* File doesn't exist yet, no messages */
        pthread_mutex_unlock(&g_message_file_lock);
        *messages = msg_array;
        return 0;
    }

    /* Seek to end */
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        pthread_mutex_unlock(&g_message_file_lock);
        *messages = msg_array;
        return 0;
    }

    long file_size = ftell(fp);
    if (file_size <= 0) {
        fclose(fp);
        pthread_mutex_unlock(&g_message_file_lock);
        *messages = msg_array;
        return 0;
    }

    /* Scan backwards to find the start position */
    int newlines_found = 0;
    long pos = file_size - 1;
    /* Skip the very last byte if it's a newline */
    if (pos >= 0) {
        /* Read last char */
        fseek(fp, pos, SEEK_SET);
        if (fgetc(fp) == '\n') {
            pos--;
        }
    }

    /* Read backwards in chunks for performance */
    #define CHUNK_SIZE 4096
    char chunk[CHUNK_SIZE];
    
    while (pos >= 0 && newlines_found < max_messages) {
        long read_size = (pos >= CHUNK_SIZE) ? CHUNK_SIZE : (pos + 1);
        long read_pos = pos - read_size + 1;
        
        fseek(fp, read_pos, SEEK_SET);
        if (fread(chunk, 1, read_size, fp) != (size_t)read_size) {
            break;
        }
        
        /* Scan chunk backwards */
        for (int i = read_size - 1; i >= 0; i--) {
            if (chunk[i] == '\n') {
                newlines_found++;
                if (newlines_found >= max_messages) {
                    /* Found our start point: one char after this newline */
                    fseek(fp, read_pos + i + 1, SEEK_SET);
                    goto read_messages;
                }
            }
        }
        
        pos -= read_size;
    }
    
    /* If we got here, we reached start of file or didn't find enough newlines */
    fseek(fp, 0, SEEK_SET);

read_messages:;
    char line[MESSAGE_LOG_MAX_LINE];
    int count = 0;
    time_t now = time(NULL);

    /* Now read forward */
    while (fgets(line, sizeof(line), fp) && count < max_messages) {
        /* Check for oversized lines */
        size_t line_len = strlen(line);
        if (line_len >= sizeof(line) - 1 && line[line_len - 1] != '\n') {
            discard_line_remainder(fp);
            continue;
        }

        message_t parsed;
        if (!message_log_parse_record(line, &parsed, now)) {
            continue;
        }

        msg_array[count++] = parsed;
    }

    fclose(fp);
    pthread_mutex_unlock(&g_message_file_lock);
    *messages = msg_array;
    return count;
}

/* Save a message to log file */
int message_save(const message_t *msg) {
    char log_path[PATH_MAX];
    message_t safe_msg;
    char record[MAX_USERNAME_LEN + MAX_MESSAGE_LEN + 48];
    size_t record_len = 0;
    int rc = 0;

    if (tnt_state_path(log_path, sizeof(log_path), LOG_FILE) < 0) {
        return -1;
    }

    pthread_mutex_lock(&g_message_file_lock);

    FILE *fp = fopen(log_path, "a");
    if (!fp) {
        pthread_mutex_unlock(&g_message_file_lock);
        return -1;
    }

    /* Sanitize username and content to prevent log injection */
    safe_msg.timestamp = msg->timestamp;
    strncpy(safe_msg.username, msg->username, sizeof(safe_msg.username) - 1);
    safe_msg.username[sizeof(safe_msg.username) - 1] = '\0';

    strncpy(safe_msg.content, msg->content, sizeof(safe_msg.content) - 1);
    safe_msg.content[sizeof(safe_msg.content) - 1] = '\0';

    /* Replace pipe characters and newlines to prevent log format corruption */
    for (char *p = safe_msg.username; *p; p++) {
        if (*p == '|' || *p == '\n' || *p == '\r') {
            *p = '_';
        }
    }
    for (char *p = safe_msg.content; *p; p++) {
        if (*p == '|' || *p == '\n' || *p == '\r') {
            *p = ' ';
        }
    }

    if (message_log_format_record(&safe_msg, record, sizeof(record),
                                  &record_len) < 0 ||
        fwrite(record, 1, record_len, fp) != record_len ||
        fflush(fp) != 0) {
        rc = -1;
    }

    /* Rotate if the log exceeds MAX_LOG_SIZE */
    long file_size = ftell(fp);
    fclose(fp);

    if (file_size > MAX_LOG_SIZE) {
        char backup_path[PATH_MAX + 4];
        snprintf(backup_path, sizeof(backup_path), "%s.1", log_path);
        rename(log_path, backup_path);
    }

    pthread_mutex_unlock(&g_message_file_lock);
    return rc;
}

/* Search log file for messages whose username or content contains query.
 * Case-insensitive. Returns the last max_results matches (most recent); caller frees *results. */
int message_search(const char *query, message_t **results, int max_results) {
    char log_path[PATH_MAX];

    message_t *res = calloc(max_results, sizeof(message_t));
    if (!res) return 0;

    if (!query || query[0] == '\0' ||
        tnt_state_path(log_path, sizeof(log_path), LOG_FILE) < 0) {
        *results = res;
        return 0;
    }

    pthread_mutex_lock(&g_message_file_lock);
    FILE *fp = fopen(log_path, "r");
    if (!fp) {
        pthread_mutex_unlock(&g_message_file_lock);
        *results = res;
        return 0;
    }

    char line[MESSAGE_LOG_MAX_LINE];
    int count = 0;
    time_t now = time(NULL);

    while (fgets(line, sizeof(line), fp)) {
        size_t line_len = strlen(line);
        if (line_len >= sizeof(line) - 1 && line[line_len - 1] != '\n') {
            discard_line_remainder(fp);
            continue;
        }

        message_t m;
        if (!message_log_parse_record(line, &m, now)) continue;
        if (strcasestr(m.username, query) == NULL &&
            strcasestr(m.content, query) == NULL) continue;

        if (count < max_results) {
            res[count++] = m;
        } else {
            memmove(&res[0], &res[1], (max_results - 1) * sizeof(message_t));
            res[max_results - 1] = m;
            /* count stays at max_results */
        }
    }

    fclose(fp);
    pthread_mutex_unlock(&g_message_file_lock);
    *results = res;
    return (count < max_results) ? count : max_results;
}

int message_dump_text(char **output, size_t *output_len, int max_records) {
    char log_path[PATH_MAX];
    char *buf = NULL;
    size_t capacity = 0;
    size_t len = 0;
    message_t *ring = NULL;
    int seen = 0;
    int rc = 0;

    if (!output || !output_len || max_records < 0) {
        return -1;
    }

    *output = calloc(1, 1);
    if (!*output) {
        return -1;
    }
    *output_len = 0;

    if (tnt_state_path(log_path, sizeof(log_path), LOG_FILE) < 0) {
        free(*output);
        *output = NULL;
        return -1;
    }

    if (max_records > 0) {
        ring = calloc((size_t)max_records, sizeof(*ring));
        if (!ring) {
            free(*output);
            *output = NULL;
            return -1;
        }
    }

    pthread_mutex_lock(&g_message_file_lock);
    FILE *fp = fopen(log_path, "r");
    if (!fp) {
        int saved_errno = errno;
        pthread_mutex_unlock(&g_message_file_lock);
        free(ring);
        if (saved_errno != ENOENT) {
            free(*output);
            *output = NULL;
            return -1;
        }
        return 0;
    }

    char line[MESSAGE_LOG_MAX_LINE];
    time_t now = time(NULL);
    while (fgets(line, sizeof(line), fp)) {
        size_t line_len = strlen(line);
        if (line_len >= sizeof(line) - 1 && line[line_len - 1] != '\n') {
            discard_line_remainder(fp);
            continue;
        }

        message_t parsed;
        if (!message_log_parse_record(line, &parsed, now)) {
            continue;
        }

        if (max_records > 0) {
            ring[seen % max_records] = parsed;
            seen++;
        } else if (append_dump_record(output, &capacity, output_len,
                                      &parsed) < 0) {
            rc = -1;
            break;
        }
    }

    fclose(fp);
    pthread_mutex_unlock(&g_message_file_lock);

    if (rc == 0 && max_records > 0 && seen > 0) {
        int count = seen < max_records ? seen : max_records;
        int start = seen < max_records ? 0 : seen % max_records;

        free(*output);
        *output = NULL;
        *output_len = 0;

        for (int i = 0; i < count; i++) {
            message_t *msg = &ring[(start + i) % max_records];
            if (append_dump_record(&buf, &capacity, &len, msg) < 0) {
                rc = -1;
                break;
            }
        }

        if (rc == 0) {
            *output = buf ? buf : calloc(1, 1);
            *output_len = len;
            if (!*output) {
                rc = -1;
            }
        } else {
            free(buf);
        }
    }

    free(ring);
    if (rc < 0) {
        free(*output);
        *output = NULL;
        *output_len = 0;
        return -1;
    }
    return 0;
}

/* Format a message for display */
void message_format(const message_t *msg, char *buffer, size_t buf_size, int width) {
    struct tm tm_info;
    localtime_r(&msg->timestamp, &tm_info);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M %Z", &tm_info);

    int written = snprintf(buffer, buf_size, "[%s] %s: %s", time_str, msg->username, msg->content);

    /* If snprintf truncated, the last UTF-8 character may be incomplete.
     * Re-validate and trim any trailing partial sequence. */
    if (written >= (int)buf_size) {
        size_t len = strlen(buffer);
        while (len > 0 && (buffer[len - 1] & 0xC0) == 0x80) {
            len--;  /* walk back continuation bytes */
        }
        if (len > 0 && (unsigned char)buffer[len - 1] >= 0xC0) {
            /* This is a start byte whose sequence was truncated */
            buffer[len - 1] = '\0';
        }
    }

    /* Truncate to terminal width */
    if (utf8_string_width(buffer) > width) {
        utf8_truncate(buffer, width);
    }
}
