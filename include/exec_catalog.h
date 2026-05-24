#ifndef EXEC_CATALOG_H
#define EXEC_CATALOG_H

#include "common.h"

typedef enum {
    TNT_EXEC_COMMAND_HELP,
    TNT_EXEC_COMMAND_HEALTH,
    TNT_EXEC_COMMAND_USERS,
    TNT_EXEC_COMMAND_STATS,
    TNT_EXEC_COMMAND_TAIL,
    TNT_EXEC_COMMAND_POST,
    TNT_EXEC_COMMAND_EXIT
} tnt_exec_command_id_t;

bool exec_catalog_match(const char *line, tnt_exec_command_id_t *id,
                        const char **args);
bool exec_catalog_args_valid(tnt_exec_command_id_t id, const char *args);
void exec_catalog_append_help(char *buffer, size_t buf_size, size_t *pos,
                              ui_lang_t lang);
void exec_catalog_append_usage(char *buffer, size_t buf_size, size_t *pos,
                               tnt_exec_command_id_t id, ui_lang_t lang);

#endif /* EXEC_CATALOG_H */
