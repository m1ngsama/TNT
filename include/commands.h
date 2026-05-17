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
 * Reads g_room.  Caller must already hold the channel I/O serialisation
 * established by handle_key() — this function calls back into client_send
 * (via tui_render_command_output) which acquires client->io_lock. */
void commands_dispatch(client_t *client);

#endif /* COMMANDS_H */
