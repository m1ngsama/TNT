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

/* Render the input line */
void tui_render_input(struct client *client, const char *input);

/* Clear the screen */
void tui_clear_screen(struct client *client);

/* Get help text based on language */
const char* tui_get_help_text(help_lang_t lang);

#endif /* TUI_H */
