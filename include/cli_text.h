#ifndef CLI_TEXT_H
#define CLI_TEXT_H

#include "common.h"

void cli_text_append_help(char *buffer, size_t buf_size, size_t *pos,
                          const char *program_name, ui_lang_t lang);
const char *cli_text_invalid_port_format(ui_lang_t lang);
const char *cli_text_invalid_value_format(ui_lang_t lang);
const char *cli_text_option_requires_arg_format(ui_lang_t lang);
const char *cli_text_unknown_option_format(ui_lang_t lang);
const char *cli_text_short_usage_format(ui_lang_t lang);

#endif /* CLI_TEXT_H */
