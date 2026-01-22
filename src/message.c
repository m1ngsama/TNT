#include "message.h"
#include "utf8.h"
#include <unistd.h>
#include <fcntl.h>

/* Initialize message subsystem */
void message_init(void) {
    /* Nothing to initialize for now */
}

/* Load messages from log file */
int message_load(message_t **messages, int max_messages) {
    /* Always allocate the message array */
    message_t *msg_array = calloc(max_messages, sizeof(message_t));
    if (!msg_array) {
        return 0;
    }

    FILE *fp = fopen(LOG_FILE, "r");
    if (!fp) {
        /* File doesn't exist yet, no messages */
        *messages = msg_array;
        return 0;
    }

    char line[2048];
    int count = 0;

    /* Use a ring buffer approach - keep only last max_messages */
    /* First, count total lines and seek to appropriate position */
    /* Use dynamic allocation to handle large log files */
    long *file_pos = NULL;
    int pos_capacity = 1000;
    int line_count = 0;
    int start_index = 0;

    /* Allocate initial position array */
    file_pos = malloc(pos_capacity * sizeof(long));
    if (!file_pos) {
        fclose(fp);
        *messages = msg_array;
        return 0;
    }

    /* Record file positions */
    while (fgets(line, sizeof(line), fp)) {
        /* Expand array if needed */
        if (line_count >= pos_capacity) {
            int new_capacity = pos_capacity * 2;
            long *new_pos = realloc(file_pos, new_capacity * sizeof(long));
            if (!new_pos) {
                /* Out of memory, stop scanning */
                break;
            }
            file_pos = new_pos;
            pos_capacity = new_capacity;
        }
        file_pos[line_count++] = ftell(fp) - strlen(line);
    }

    /* Determine where to start reading */
    if (line_count > max_messages) {
        start_index = line_count - max_messages;
        fseek(fp, file_pos[start_index], SEEK_SET);
    } else {
        fseek(fp, 0, SEEK_SET);
        start_index = 0;
    }

    /* Now read the messages */
    while (fgets(line, sizeof(line), fp) && count < max_messages) {
        /* Format: RFC3339_timestamp|username|content */
        char line_copy[2048];
        strncpy(line_copy, line, sizeof(line_copy) - 1);
        line_copy[sizeof(line_copy) - 1] = '\0';

        char *timestamp_str = strtok(line_copy, "|");
        char *username = strtok(NULL, "|");
        char *content = strtok(NULL, "\n");

        if (!timestamp_str || !username || !content) {
            continue;
        }

        /* Parse ISO 8601 timestamp */
        struct tm tm = {0};
        char *result = strptime(timestamp_str, "%Y-%m-%dT%H:%M:%S", &tm);
        if (!result) {
            continue;
        }

        msg_array[count].timestamp = mktime(&tm);
        strncpy(msg_array[count].username, username, MAX_USERNAME_LEN - 1);
        strncpy(msg_array[count].content, content, MAX_MESSAGE_LEN - 1);
        count++;
    }

    free(file_pos);
    fclose(fp);
    *messages = msg_array;
    return count;
}

/* Save a message to log file */
int message_save(const message_t *msg) {
    FILE *fp = fopen(LOG_FILE, "a");
    if (!fp) {
        return -1;
    }

    /* Format timestamp as RFC3339 */
    char timestamp[64];
    struct tm tm_info;
    gmtime_r(&msg->timestamp, &tm_info);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &tm_info);

    /* Write to file: timestamp|username|content */
    fprintf(fp, "%s|%s|%s\n", timestamp, msg->username, msg->content);

    fclose(fp);
    return 0;
}

/* Format a message for display */
void message_format(const message_t *msg, char *buffer, size_t buf_size, int width) {
    struct tm tm_info;
    localtime_r(&msg->timestamp, &tm_info);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M %Z", &tm_info);

    snprintf(buffer, buf_size, "[%s] %s: %s", time_str, msg->username, msg->content);

    /* Truncate if too long */
    if (utf8_string_width(buffer) > width) {
        utf8_truncate(buffer, width);
    }
}
