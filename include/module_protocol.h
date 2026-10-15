#ifndef MODULE_PROTOCOL_H
#define MODULE_PROTOCOL_H

#include "identity.h"
#include "message.h"

#define TNT_MODULE_PROTOCOL_VERSION "tnt.module.v1"
#define TNT_MODULE_EVENT_MESSAGE_CREATED "message.created"
#define TNT_MODULE_RESPONSE_MESSAGE_CREATE "message.create"
#define TNT_MODULE_RECORD_MESSAGE_POST "message.post"
#define TNT_MODULE_EVENT_PRESENCE_JOINED "presence.joined"
#define TNT_MODULE_EVENT_PRESENCE_LEFT "presence.left"
#define TNT_MODULE_EVENT_PRESENCE_SNAPSHOT "presence.snapshot"
#define TNT_MODULE_EVENT_IDENTITY_REQUESTED "identity.requested"
#define TNT_MODULE_EVENT_IDENTITY_RECOGNIZED "identity.recognized"
#define TNT_MODULE_EVENT_IDENTITY_KEY_BOUND "identity.key_bound"
#define TNT_MODULE_EVENT_IDENTITY_SIGNED_OUT "identity.signed_out"
#define TNT_MODULE_RECORD_IDENTITY_PREFIX "identity."
#define TNT_MODULE_RECORD_PRESENCE_REMOTE "presence.remote"
#define TNT_MODULE_RECORD_PRIVATE_POST "private.post"
#define TNT_MODULE_EVENT_PRIVATE_CREATED "private.created"
#define TNT_MODULE_REMOTE_USERS_MAX 256
#define TNT_MODULE_SESSION_ID_SIZE 32

typedef struct {
    char plain_text[MAX_MESSAGE_LEN];
} tnt_module_message_create_t;

typedef struct {
    char sender[MAX_USERNAME_LEN];
    char plain_text[MAX_MESSAGE_LEN];
} tnt_module_message_post_t;

typedef enum {
    TNT_MODULE_IDENTITY_NOTICE,
    TNT_MODULE_IDENTITY_VERIFIED,
    TNT_MODULE_IDENTITY_RESERVE
} tnt_module_identity_kind_t;

typedef struct {
    tnt_module_identity_kind_t kind;
    char session[TNT_MODULE_SESSION_ID_SIZE];
    char handle[TNT_IDENTITY_HANDLE_MAX + 1];
    char plain_text[MAX_MESSAGE_LEN];
} tnt_module_identity_record_t;

typedef struct {
    char nicknames[TNT_MODULE_REMOTE_USERS_MAX][MAX_USERNAME_LEN];
    size_t count;
} tnt_module_presence_remote_t;

typedef struct {
    char from[MAX_USERNAME_LEN];
    char to[MAX_USERNAME_LEN];
    char plain_text[MAX_MESSAGE_LEN];
} tnt_module_private_post_t;

int tnt_module_append_handshake(char *buffer, size_t buf_size, size_t *pos,
                                const char *server_version);

int tnt_module_append_message_created(char *buffer, size_t buf_size,
                                      size_t *pos, const char *message_id,
                                      const message_t *msg);

int tnt_module_append_presence(char *buffer, size_t buf_size, size_t *pos,
                               const char *type, const char *nickname,
                               time_t timestamp);

int tnt_module_append_presence_snapshot(char *buffer, size_t buf_size,
                                        size_t *pos,
                                        const char *const *nicknames,
                                        size_t count);

int tnt_module_append_identity_requested(char *buffer, size_t buf_size,
                                         size_t *pos, const char *session,
                                         const char *nickname);

int tnt_module_append_identity_recognized(char *buffer, size_t buf_size,
                                          size_t *pos, const char *session,
                                          const char *handle,
                                          const char *fingerprint);

/* An empty or NULL `previous_handle` means the key was bound to no handle. */
int tnt_module_append_identity_key_bound(char *buffer, size_t buf_size,
                                         size_t *pos, const char *session,
                                         const char *handle,
                                         const char *fingerprint,
                                         const char *previous_handle);

/* An empty or NULL `forgotten_fingerprint` means no key was forgotten. */
int tnt_module_append_identity_signed_out(char *buffer, size_t buf_size,
                                          size_t *pos, const char *session,
                                          const char *handle,
                                          const char *forgotten_fingerprint);

int tnt_module_append_private_created(char *buffer, size_t buf_size,
                                      size_t *pos, const char *from,
                                      const char *to, const char *plain_text,
                                      time_t timestamp, bool verified);

bool tnt_module_parse_message_create(const char *line,
                                     tnt_module_message_create_t *out);

bool tnt_module_parse_message_post(const char *line,
                                   tnt_module_message_post_t *out);

bool tnt_module_parse_identity_record(const char *line,
                                      tnt_module_identity_record_t *out);

/* Names repeated without regard to ASCII letter case are kept once. */
bool tnt_module_parse_presence_remote(const char *line,
                                      tnt_module_presence_remote_t *out);

bool tnt_module_parse_private_post(const char *line,
                                   tnt_module_private_post_t *out);

#endif /* MODULE_PROTOCOL_H */
