#ifndef BOOTSTRAP_H
#define BOOTSTRAP_H

#include "ssh_server.h"  /* for client_t and the libssh / arpa includes */

/* Hand-off envelope between the accept loop and the bootstrap thread.
 * The accept loop allocates one of these per accepted session, fills it,
 * and pthread_create()s a detached bootstrap_run() with this pointer.
 * bootstrap_run() owns the struct and the embedded ssh_session, and frees
 * both before returning. */
typedef struct {
    ssh_session session;
    char client_ip[INET6_ADDRSTRLEN];
} accepted_session_t;

/* Read TNT_ACCESS_TOKEN from the environment.  Idempotent.  Call once
 * during startup, before bootstrap_run() can fire on any accepted
 * session. */
void bootstrap_init(void);

/* Read the peer IP off an accepted ssh_session into ip_buf.  Sets ip_buf
 * to "unknown" when the address family is unrecognised or getpeername()
 * fails.  ip_buf must be at least INET6_ADDRSTRLEN bytes. */
void bootstrap_peer_ip(ssh_session session, char *ip_buf, size_t buf_size);

/* pthread entry point for the per-connection bootstrap thread.
 *
 * Steps performed before handing control to input_run_session():
 *   1. SSH key exchange
 *   2. auth (password / none / pubkey, with rate-limit feedback)
 *   3. channel open + PTY/shell-or-exec request
 *   4. construct a client_t and install its lifetime channel callbacks
 *
 * On any failure path the connection is torn down and ratelimit /
 * connection counters are released; input_run_session() is never
 * invoked.  Always returns NULL. */
void *bootstrap_run(void *arg);

#endif /* BOOTSTRAP_H */
