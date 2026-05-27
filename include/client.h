#ifndef CLIENT_H
#define CLIENT_H

#include "ssh_server.h"  /* for client_t */

/* Send `len` bytes to the client over its SSH channel.
 *
 * Exec sessions write synchronously so command output and exit status remain
 * ordered.  Interactive sessions enqueue into a bounded per-client outbox and
 * flush opportunistically from the same client's session loop, so a closed SSH
 * window cannot block unrelated room activity.  Returns -1 if the channel is
 * gone, a write fails, or the bounded outbox is full. */
int client_send(client_t *client, const char *data, size_t len);

/* Flush queued interactive output for this client.  Returns 0 when all
 * possible progress was made; queued bytes may remain if the remote SSH window
 * is currently closed. */
int client_flush_output(client_t *client);

/* Queue an audible bell for the client's own session loop to send.  This
 * avoids writing to another client's SSH channel from the sender's thread. */
void client_queue_bell(client_t *client);

/* Send one queued bell, if present, from the client's own session loop.
 * Returns 0 when no bell was pending or it was written successfully. */
int client_flush_pending_bells(client_t *client);

/* printf-style wrapper around client_send().  The formatted string must
 * fit in 2048 bytes; truncation or encoding errors return -1. */
int client_printf(client_t *client, const char *fmt, ...);

/* Reference counting for safe cross-thread cleanup.
 *
 * Lifecycle: bootstrap_run() creates the client_t with ref_count = 1
 * (the "main" ref).  client_install_channel_callbacks() takes a second
 * ref owned by client.c while channel callbacks are installed, so the
 * client outlives in-flight eof / close / window-change callbacks.
 * input_run_session() ends ownership with client_release_session(). */
void client_addref(client_t *client);
void client_release(client_t *client);
void client_release_session(client_t *client);

/* Install the post-bootstrap channel callbacks (window-change, eof, close).
 * On success this function takes the callback reference described above.
 * On failure no callback reference remains and the caller still owns only
 * its original main reference. */
int client_install_channel_callbacks(client_t *client);

#endif /* CLIENT_H */
