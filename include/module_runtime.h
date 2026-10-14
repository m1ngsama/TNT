#ifndef MODULE_RUNTIME_H
#define MODULE_RUNTIME_H

#include "message.h"

#define TNT_MAX_MODULES 8
#define TNT_MODULE_QUEUE_LIMIT 128
/* Module-created messages use "module:<name>" as the public sender. Keep the
 * module id short enough to fit message_t.username including the NUL byte. */
#define TNT_MODULE_NAME_MAX (MAX_USERNAME_LEN - 8)

typedef struct {
    char name[64];
    char entrypoint[PATH_MAX];
    bool wants_message_created;
    bool can_read_messages;
    bool can_create_messages;
    bool can_post_messages;
    bool can_read_presence;
    bool can_write_identity;
    bool can_message_private;
} tnt_module_manifest_t;

int tnt_module_manifest_load(const char *module_dir,
                             tnt_module_manifest_t *out);

int tnt_module_runtime_init(void);
void tnt_module_runtime_shutdown(void);

/* Queue a user/core-created public message for enabled modules. This is
 * intentionally fire-and-forget so basic chat never depends on module health. */
void tnt_module_runtime_publish_message_created(const message_t *msg,
                                                bool verified);

/* Track an interactive user joining or leaving and notify presence:read
 * modules.  Safe to call with the room lock held. */
void tnt_module_runtime_publish_presence(const char *nickname, bool joined);

/* Ask the identity:write module to sign in an interactive session.  Returns
 * false when no such module can take the request. */
bool tnt_module_runtime_request_identity(const char *session,
                                         const char *nickname);

/* Copy up to `capacity` names of the users the message:private module reports
 * as online elsewhere.  Returns the number copied. */
int tnt_module_runtime_copy_remote_users(char (*out)[MAX_USERNAME_LEN],
                                         int capacity);
int tnt_module_runtime_remote_user_count(void);

/* Hand a private message for a remote user to the message:private module.
 * `to` matches remote names without regard to ASCII letter case.  Returns
 * false when no remote user has that name or the module cannot take it. */
bool tnt_module_runtime_send_private(const char *from, const char *to,
                                     const char *plain_text, bool verified);

#ifdef TNT_TESTING
/* Pipe-level test seams for deterministic backpressure/deadline coverage. */
int tnt_module_runtime_test_write_fd(int fd, const char *data, size_t len,
                                     int timeout_ms, bool cancel_on_stop);
int tnt_module_runtime_test_read_fd(int fd, char *line, size_t line_size,
                                    int timeout_ms, bool cancel_on_stop);
int tnt_module_runtime_test_read_pair_fd(int fd, char *first,
                                         size_t first_size, char *second,
                                         size_t second_size, int timeout_ms);
void tnt_module_runtime_test_reset_stop(void);
#endif

#endif /* MODULE_RUNTIME_H */
