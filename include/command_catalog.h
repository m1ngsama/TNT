#ifndef COMMAND_CATALOG_H
#define COMMAND_CATALOG_H

#include "common.h"

typedef enum {
    TNT_COMMAND_USERS,
    TNT_COMMAND_HELP,
    TNT_COMMAND_LANG,
    TNT_COMMAND_MSG,
    TNT_COMMAND_REPLY,
    TNT_COMMAND_INBOX,
    TNT_COMMAND_NICK,
    TNT_COMMAND_LAST,
    TNT_COMMAND_SEARCH,
    TNT_COMMAND_MUTE_JOINS,
    TNT_COMMAND_THEME,
    TNT_COMMAND_QUIT,
    TNT_COMMAND_CLEAR,
    TNT_COMMAND_COUNT
} tnt_command_id_t;

typedef struct {
    tnt_command_id_t id;
    const char *canonical;
    const char *names[4];
} tnt_command_spec_t;

const tnt_command_spec_t *command_catalog_get(tnt_command_id_t id);
bool command_catalog_match(const char *line, tnt_command_id_t *id,
                           const char **args);
bool command_catalog_args_valid(tnt_command_id_t id, const char *args);
const char *command_catalog_suggest(const char *name);

/* Prefix-complete a command name for Tab completion.
 *
 * Case-insensitively matches `prefix` against canonical command names.  An
 * empty or NULL prefix matches every command.  Up to `max` matching canonical
 * names are written to `out` (pointers to static storage).  The longest common
 * prefix of *all* matches is written to `lcp` (NUL-terminated, truncated to
 * `lcp_size`); it is empty when matches share no common prefix.  Returns the
 * total number of matches (which may exceed `max`). */
size_t command_catalog_complete(const char *prefix, const char **out,
                                size_t max, char *lcp, size_t lcp_size);
void command_catalog_append_full(char *buffer, size_t buf_size, size_t *pos,
                                 ui_lang_t lang);
void command_catalog_append_manual(char *buffer, size_t buf_size, size_t *pos,
                                   ui_lang_t lang);
void command_catalog_append_usage(char *buffer, size_t buf_size, size_t *pos,
                                  tnt_command_id_t id, ui_lang_t lang);

#endif /* COMMAND_CATALOG_H */
