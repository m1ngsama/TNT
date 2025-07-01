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
    int total = 0;

    /* Read all messages first to get total count */
    message_t *temp_array = calloc(max_messages * 10, sizeof(message_t));
    if (!temp_array) {
        *messages = msg_array;  /* Set the allocated array even on error */
        fclose(fp);
        return 0;
    }

    while (fgets(line, sizeof(line), fp) && total < max_messages * 10) {
        /* Format: RFC3339_timestamp|username|content */
        char *timestamp_str = strtok(line, "|");
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

        temp_array[total].timestamp = mktime(&tm);
        strncpy(temp_array[total].username, username, MAX_USERNAME_LEN - 1);
        strncpy(temp_array[total].content, content, MAX_MESSAGE_LEN - 1);
        total++;
    }

    fclose(fp);

    /* Keep only the last max_messages */
    int start = (total > max_messages) ? (total - max_messages) : 0;
    count = total - start;

    for (int i = 0; i < count; i++) {
        msg_array[i] = temp_array[start + i];
    }

    free(temp_array);
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
