#ifndef INPUT_H
#define INPUT_H

#include "ssh_server.h"  /* for client_t */

/* Read TNT_IDLE_TIMEOUT from the environment.  Idempotent.  Call once at
 * startup before any session can run. */
void input_init(void);

/* Run the interactive session for an already-bootstrapped client_t.
 *
 * Sequence:
 *   1. If client->exec_command is set, dispatch it via exec_dispatch and
 *      return (no chat-room join).
 *   2. Read the desired username from the channel.
 *   3. Add the client to g_room and broadcast a system join message.
 *   4. Optionally show the MOTD if state-dir/motd.txt exists.
 *   5. Drive the keyboard / room-update / keepalive / idle-timeout loop
 *      until the client disconnects.
 *   6. Broadcast a system leave message and release all refs / counters.
 *
 * Owns the client_t after entry: callers must NOT touch it once this
 * returns.  Always returns regardless of how the session ended. */
void input_run_session(client_t *client);

/* Bell-notify any clients whose @username appears in the broadcast
 * content, skipping the sender.  Used by the INSERT-mode send path
 * inside input_run_session and by exec_command_post. */
void notify_mentions(const char *content, const client_t *sender);

/* Module-facing identity hooks, called from the identity module's worker.
 * Each returns false when no interactive session has that id. */
bool input_identity_notice(const char *session, const char *plain_text);
bool input_identity_verified(const char *session, const char *handle);

#endif /* INPUT_H */
