#ifndef SUPPORT_H
#define SUPPORT_H

#include "common.h"

void support_append_interactive_panel(char *buffer, size_t buf_size,
                                      size_t *pos);
void support_append_exec_panel(char *buffer, size_t buf_size, size_t *pos);

#endif /* SUPPORT_H */
