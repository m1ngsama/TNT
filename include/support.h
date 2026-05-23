#ifndef SUPPORT_H
#define SUPPORT_H

#include "common.h"

void support_append_interactive_panel(char *buffer, size_t buf_size,
                                      size_t *pos, help_lang_t lang);
void support_append_exec_panel(char *buffer, size_t buf_size, size_t *pos,
                               help_lang_t lang);

#endif /* SUPPORT_H */
