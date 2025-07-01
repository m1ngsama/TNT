#ifndef MESSAGE_H
#define MESSAGE_H

#include "common.h"

/* Message structure */
typedef struct {
    time_t timestamp;
    char username[MAX_USERNAME_LEN];
    char content[MAX_MESSAGE_LEN];
} message_t;

/* Initialize message subsystem */
void message_init(void);

/* Load messages from log file */
int message_load(message_t **messages, int max_messages);

/* Save a message to log file */
int message_save(const message_t *msg);

/* Format a message for display */
void message_format(const message_t *msg, char *buffer, size_t buf_size, int width);

#endif /* MESSAGE_H */
