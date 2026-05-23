#ifndef HELP_TEXT_H
#define HELP_TEXT_H

#include "common.h"

const char *help_text_full(help_lang_t lang);
void help_text_append_commands(char *output, size_t buf_size, size_t *pos,
                               help_lang_t lang);

#endif /* HELP_TEXT_H */
