#ifndef I18N_H
#define I18N_H

#include "common.h"

typedef enum {
    I18N_USERNAME_PROMPT,
    I18N_INVALID_USERNAME,
    I18N_ROOM_FULL,
    I18N_INSERT_HINT_WIDE,
    I18N_INSERT_HINT_NARROW,
    I18N_NORMAL_LATEST,
    I18N_NORMAL_NEW_MESSAGES,
    I18N_HELP_TITLE,
    I18N_HELP_STATUS_FORMAT
} i18n_text_id_t;

bool i18n_try_parse_lang(const char *value, help_lang_t *lang);
help_lang_t i18n_parse_lang(const char *value, help_lang_t fallback);
help_lang_t i18n_default_lang(void);
const char *i18n_lang_code(help_lang_t lang);
const char *i18n_text(help_lang_t lang, i18n_text_id_t id);

#endif /* I18N_H */
