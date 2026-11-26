#ifndef MESSAGE_H
#define MESSAGE_H

#include "common.h"

/* Message structure */
typedef struct {
    time_t timestamp;
    char username[MAX_USERNAME_LEN];
    char content[MAX_MESSAGE_LEN];
    bool verified;                   /* The sender proved its identity */
    char room[TNT_ROOM_NAME_MAX + 1]; /* Empty for the default room */
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

/* True for an empty name and for the default room's name. */
static inline bool message_room_is_default(const char *room) {
    return !room || room[0] == '\0' || strcmp(room, TNT_DEFAULT_ROOM) == 0;
}

/* The state-directory log file of a room: messages.log for the default room,
 * room-NAME.log for any other. */
void message_log_file_for(const char *room, char *out, size_t out_size);

/* Load messages from log file */
int message_load(message_t **messages, int max_messages);

/* Save a message to its room's log file */
int message_save(const message_t *msg);

/* The same, for a log file in the state directory other than messages.log. */
int message_load_from(const char *log_file, message_t **messages,
                      int max_messages);
int message_search_in(const char *log_file, const char *query,
                      message_t **results, int max_results);

/* Format a message for display */
void message_format(const message_t *msg, char *buffer, size_t buf_size, int width);

/* Search log file for messages matching query (case-insensitive, username or content).
 * Returns the last max_results matches in chronological order; caller must free *results. */
int message_search(const char *query, message_t **results, int max_results);

/* Export valid persisted log records without their flags field, as exec dump
 * always has.  max_records
 * 0 exports all valid records; positive values export the last max_records
 * valid records.  Caller must free *output. */
int message_dump_text(char **output, size_t *output_len, int max_records);

#endif /* MESSAGE_H */
