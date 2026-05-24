#include "i18n.h"

#include <ctype.h>

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

    if (starts_with_lang(value, "zh")) {
        if (lang) *lang = UI_LANG_ZH;
        return true;
    }

    if (starts_with_lang(value, "en") ||
        starts_with_lang(value, "c") ||
        starts_with_lang(value, "posix")) {
        if (lang) *lang = UI_LANG_EN;
        return true;
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

const char *i18n_ui_lang_code(ui_lang_t lang) {
    return lang == UI_LANG_ZH ? "zh" : "en";
}
