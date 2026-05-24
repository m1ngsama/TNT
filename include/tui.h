#ifndef TUI_H
#define TUI_H

#include "common.h"
#include "message.h"

/* Client structure (forward declaration) */
struct client;

/* Render the main screen */
void tui_render_screen(struct client *client);

/* Render the help screen */
void tui_render_help(struct client *client);

/* Render the command output screen */
void tui_render_command_output(struct client *client);

/* Render the MOTD screen.  Reads the message text from
 * client->command_output (shared storage); the show_motd flag selects
 * this renderer over tui_render_command_output. */
void tui_render_motd(struct client *client);

/* Render the input line */
void tui_render_input(struct client *client, const char *input);

/* Clear the screen */
void tui_clear_screen(struct client *client);

/* Render the pre-login welcome banner.  Centered, framed, shown once before
 * the username prompt.  Caller is responsible for printing the prompt
 * itself afterwards. */
void tui_render_welcome(struct client *client);

#endif /* TUI_H */
