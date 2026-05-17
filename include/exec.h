#ifndef EXEC_H
#define EXEC_H

#include "ssh_server.h"  /* for client_t */

/* Dispatch the non-interactive SSH exec command stored in
 * client->exec_command.  Returns the exit status to send back to the
 * SSH client:
 *   0  = success
 *   1  = runtime error (I/O, OOM, persistence failure)
 *  64  = usage error (unknown command, bad args)
 *
 * Reads g_room and shared client state.  Safe to call once per
 * exec-mode session before the channel is closed. */
int exec_dispatch(client_t *client);

#endif /* EXEC_H */
