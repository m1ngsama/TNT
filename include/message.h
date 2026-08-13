#ifndef MESSAGE_H
#define MESSAGE_H

#include "common.h"

/* Message structure */
typedef struct {
    time_t timestamp;
    char username[MAX_USERNAME_LEN];
    char content[MAX_MESSAGE_LEN];
    /* Cached server-local display fields.  A room message is formatted once
     * when inserted instead of making every connected TUI contend on the
     * platform timezone lock for every repaint. */
    char display_time[6];            /* HH:MM */
    char display_date[11];           /* YYYY-MM-DD */
} message_t;

/* Initialize message subsystem */
void message_init(void);

/* Populate the cached local display fields above. */
void message_prepare_display(message_t *msg);

/* Load messages from log file */
int message_load(message_t **messages, int max_messages);

/* Save a message to log file */
int message_save(const message_t *msg);

/* Format a message for display */
void message_format(const message_t *msg, char *buffer, size_t buf_size, int width);

/* Search log file for messages matching query (case-insensitive, username or content).
 * Returns the last max_results matches in chronological order; caller must free *results. */
int message_search(const char *query, message_t **results, int max_results);

/* Export valid persisted log records in messages.log v1 format.  max_records
 * 0 exports all valid records; positive values export the last max_records
 * valid records.  Caller must free *output. */
int message_dump_text(char **output, size_t *output_len, int max_records);

#endif /* MESSAGE_H */
