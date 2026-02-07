#include "message.h"
#include "utf8.h"
#include <unistd.h>
#include <fcntl.h>

/* Initialize message subsystem */
void message_init(void) {
    /* Nothing to initialize for now */
}

/* Load messages from log file - Optimized for large files */
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

    /* Seek to end */
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        *messages = msg_array;
        return 0;
    }

    long file_size = ftell(fp);
    if (file_size == 0) {
        fclose(fp);
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
    char line[2048];
    int count = 0;

    /* Now read forward */
    while (fgets(line, sizeof(line), fp) && count < max_messages) {
        /* Check for oversized lines */
        size_t line_len = strlen(line);
        if (line_len >= sizeof(line) - 1) {
            /* Skip remainder of line */
            int c;
            while ((c = fgetc(fp)) != '\n' && c != EOF);
            continue;
        }

        /* Format: RFC3339_timestamp|username|content */
        char line_copy[2048];
        strncpy(line_copy, line, sizeof(line_copy) - 1);
        line_copy[sizeof(line_copy) - 1] = '\0';

        char *timestamp_str = strtok(line_copy, "|");
        char *username = strtok(NULL, "|");
        char *content = strtok(NULL, "\n");

        /* Validate all fields exist */
        if (!timestamp_str || !username || !content) {
            continue;
        }

        /* Validate field lengths */
        if (strlen(username) >= MAX_USERNAME_LEN) {
            continue;
        }
        if (strlen(content) >= MAX_MESSAGE_LEN) {
            continue;
        }

        /* Parse ISO 8601 timestamp */
        struct tm tm = {0};
        char *result = strptime(timestamp_str, "%Y-%m-%dT%H:%M:%S", &tm);
        if (!result) {
            continue;
        }

        /* Validate timestamp is reasonable (not in far future or past) */
        time_t msg_time = mktime(&tm);
        time_t now = time(NULL);
        if (msg_time > now + 86400 || msg_time < now - 31536000 * 10) {
            continue;
        }

        msg_array[count].timestamp = msg_time;
        strncpy(msg_array[count].username, username, MAX_USERNAME_LEN - 1);
        msg_array[count].username[MAX_USERNAME_LEN - 1] = '\0';
        strncpy(msg_array[count].content, content, MAX_MESSAGE_LEN - 1);
        msg_array[count].content[MAX_MESSAGE_LEN - 1] = '\0';
        count++;
    }

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

    /* Sanitize username and content to prevent log injection */
    char safe_username[MAX_USERNAME_LEN];
    char safe_content[MAX_MESSAGE_LEN];

    strncpy(safe_username, msg->username, sizeof(safe_username) - 1);
    safe_username[sizeof(safe_username) - 1] = '\0';

    strncpy(safe_content, msg->content, sizeof(safe_content) - 1);
    safe_content[sizeof(safe_content) - 1] = '\0';

    /* Replace pipe characters and newlines to prevent log format corruption */
    for (char *p = safe_username; *p; p++) {
        if (*p == '|' || *p == '\n' || *p == '\r') {
            *p = '_';
        }
    }
    for (char *p = safe_content; *p; p++) {
        if (*p == '|' || *p == '\n' || *p == '\r') {
            *p = ' ';
        }
    }

    /* Write to file: timestamp|username|content */
    fprintf(fp, "%s|%s|%s\n", timestamp, safe_username, safe_content);

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
