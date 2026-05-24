#ifndef COMMAND_CATALOG_H
#define COMMAND_CATALOG_H

#include "common.h"

typedef enum {
    TNT_COMMAND_USERS,
    TNT_COMMAND_HELP,
    TNT_COMMAND_LANG,
    TNT_COMMAND_MSG,
    TNT_COMMAND_INBOX,
    TNT_COMMAND_NICK,
    TNT_COMMAND_LAST,
    TNT_COMMAND_SEARCH,
    TNT_COMMAND_MUTE_JOINS,
    TNT_COMMAND_QUIT,
    TNT_COMMAND_CLEAR,
    TNT_COMMAND_COUNT
} tnt_command_id_t;

typedef struct {
    tnt_command_id_t id;
    const char *canonical;
    const char *names[4];
    bool accepts_args;
} tnt_command_spec_t;

const tnt_command_spec_t *command_catalog_get(tnt_command_id_t id);
bool command_catalog_match(const char *line, tnt_command_id_t *id,
                           const char **args);
const char *command_catalog_suggest(const char *name);
void command_catalog_append_full(char *buffer, size_t buf_size, size_t *pos,
                                 help_lang_t lang);
void command_catalog_append_manual(char *buffer, size_t buf_size, size_t *pos,
                                   help_lang_t lang);

#endif /* COMMAND_CATALOG_H */
