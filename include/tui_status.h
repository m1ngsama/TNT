#ifndef TUI_STATUS_H
#define TUI_STATUS_H

#include "common.h"

struct client;

void tui_status_append(char *buffer, size_t buf_size, size_t *pos,
                       const struct client *client, int msg_count,
                       int start, int end);

#endif /* TUI_STATUS_H */
