#ifndef MODULE_PROTOCOL_H
#define MODULE_PROTOCOL_H

#include "message.h"

#define TNT_MODULE_PROTOCOL_VERSION "tnt.module.v1"
#define TNT_MODULE_EVENT_MESSAGE_CREATED "message.created"
#define TNT_MODULE_RESPONSE_MESSAGE_CREATE "message.create"

typedef struct {
    char plain_text[MAX_MESSAGE_LEN];
} tnt_module_message_create_t;

int tnt_module_append_handshake(char *buffer, size_t buf_size, size_t *pos,
                                const char *server_version);

int tnt_module_append_message_created(char *buffer, size_t buf_size,
                                      size_t *pos, const char *message_id,
                                      const message_t *msg);

bool tnt_module_parse_message_create(const char *line,
                                     tnt_module_message_create_t *out);

#endif /* MODULE_PROTOCOL_H */
