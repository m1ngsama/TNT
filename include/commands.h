#ifndef COMMANDS_H
#define COMMANDS_H

#include "ssh_server.h"  /* for client_t */

/* Dispatch the COMMAND-mode line currently in client->command_input.
 *
 * Side effects (visible to the caller):
 *   - May append to client->command_history
 *   - Resets client->command_input
 *   - Writes the rendered output into client->command_output (so the next
 *     redraw shows it), or returns the client to MODE_NORMAL on `:` then
 *     Enter on an empty line
 *   - Sets client->connected = false on `:q` / `:quit` / `:exit`
 *   - Toggles client->mute_joins on `:mute-joins`
 *   - May broadcast a system rename message on `:nick`
 *
 * Reads g_room.  Renders command output through the normal client_send()
 * path; callers must not hold client->io_lock before dispatching. */
void commands_dispatch(client_t *client);

/* Rebuild the currently visible command output when it is backed by live
 * client state, such as :inbox.  Returns true if output changed and the caller
 * should render it again. */
bool commands_refresh_active_output(client_t *client);

#endif /* COMMANDS_H */
