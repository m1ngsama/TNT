#include "i18n.h"

#include <ctype.h>

typedef struct {
    ui_lang_t lang;
    const char *code;
    const char *prefixes[4];
} ui_lang_definition_t;

static const ui_lang_definition_t ui_lang_defs[] = {
    {UI_LANG_EN, "en", {"en", "c", "posix", NULL}},
    {UI_LANG_ZH, "zh", {"zh", NULL}}
};
typedef char ui_lang_defs_must_cover_enum[
    sizeof(ui_lang_defs) / sizeof(ui_lang_defs[0]) == UI_LANG_COUNT ? 1 : -1];

static const char *skip_space(const char *value) {
    while (value && *value &&
           isspace((unsigned char)*value)) {
        value++;
    }
    return value;
}

static bool is_lang_boundary(const char *value) {
    if (*value == '\0' || *value == '_' || *value == '-' || *value == '.') {
        return true;
    }
    if (!isspace((unsigned char)*value)) {
        return false;
    }
    return *skip_space(value) == '\0';
}

static bool starts_with_lang(const char *value, const char *prefix) {
    if (!value || !prefix) return false;

    value = skip_space(value);
    while (*prefix) {
        if (tolower((unsigned char)*value) !=
            tolower((unsigned char)*prefix)) {
            return false;
        }
        value++;
        prefix++;
    }

    return is_lang_boundary(value);
}

bool i18n_try_parse_ui_lang(const char *value, ui_lang_t *lang) {
    if (!value || value[0] == '\0') {
        return false;
    }

    for (size_t i = 0; i < sizeof(ui_lang_defs) / sizeof(ui_lang_defs[0]); i++) {
        for (size_t j = 0; ui_lang_defs[i].prefixes[j]; j++) {
            if (starts_with_lang(value, ui_lang_defs[i].prefixes[j])) {
                if (lang) {
                    *lang = ui_lang_defs[i].lang;
                }
                return true;
            }
        }
    }

    return false;
}

ui_lang_t i18n_parse_ui_lang(const char *value, ui_lang_t fallback) {
    ui_lang_t lang;
    if (i18n_try_parse_ui_lang(value, &lang)) {
        return lang;
    }
    return fallback;
}

ui_lang_t i18n_default_ui_lang(void) {
    const char *explicit_lang = getenv("TNT_LANG");
    if (explicit_lang && explicit_lang[0] != '\0') {
        return i18n_parse_ui_lang(explicit_lang, UI_LANG_EN);
    }

    const char *locale = getenv("LC_ALL");
    if (!locale || locale[0] == '\0') {
        locale = getenv("LC_MESSAGES");
    }
    if (!locale || locale[0] == '\0') {
        locale = getenv("LANG");
    }

    return i18n_parse_ui_lang(locale, UI_LANG_EN);
}

ui_lang_t i18n_next_ui_lang(ui_lang_t lang) {
    size_t count = sizeof(ui_lang_defs) / sizeof(ui_lang_defs[0]);

    for (size_t i = 0; i < count; i++) {
        if (ui_lang_defs[i].lang == lang) {
            return ui_lang_defs[(i + 1) % count].lang;
        }
    }

    return ui_lang_defs[0].lang;
}

const char *i18n_ui_lang_code(ui_lang_t lang) {
    for (size_t i = 0; i < sizeof(ui_lang_defs) / sizeof(ui_lang_defs[0]); i++) {
        if (ui_lang_defs[i].lang == lang) {
            return ui_lang_defs[i].code;
        }
    }

    return ui_lang_defs[0].code;
}
