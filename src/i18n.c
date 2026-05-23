#include "i18n.h"

#include <ctype.h>

static bool starts_with_lang(const char *value, const char *prefix) {
    if (!value || !prefix) return false;

    while (*prefix) {
        if (tolower((unsigned char)*value) !=
            tolower((unsigned char)*prefix)) {
            return false;
        }
        value++;
        prefix++;
    }

    return *value == '\0' || *value == '_' || *value == '-' || *value == '.';
}

help_lang_t i18n_parse_lang(const char *value, help_lang_t fallback) {
    if (!value || value[0] == '\0') {
        return fallback;
    }

    if (starts_with_lang(value, "zh") ||
        starts_with_lang(value, "cn") ||
        starts_with_lang(value, "chinese")) {
        return LANG_ZH;
    }

    if (starts_with_lang(value, "en") ||
        starts_with_lang(value, "c") ||
        starts_with_lang(value, "posix")) {
        return LANG_EN;
    }

    return fallback;
}

help_lang_t i18n_default_lang(void) {
    const char *explicit_lang = getenv("TNT_LANG");
    if (explicit_lang && explicit_lang[0] != '\0') {
        return i18n_parse_lang(explicit_lang, LANG_EN);
    }

    const char *locale = getenv("LC_ALL");
    if (!locale || locale[0] == '\0') {
        locale = getenv("LC_MESSAGES");
    }
    if (!locale || locale[0] == '\0') {
        locale = getenv("LANG");
    }

    return i18n_parse_lang(locale, LANG_EN);
}

const char *i18n_lang_code(help_lang_t lang) {
    return lang == LANG_ZH ? "zh" : "en";
}

const char *i18n_text(help_lang_t lang, i18n_text_id_t id) {
    if (lang == LANG_ZH) {
        switch (id) {
            case I18N_USERNAME_PROMPT:
                return "  请输入用户名 (留空 anonymous): ";
            case I18N_INVALID_USERNAME:
                return "用户名无效，已改用 anonymous。\r\n";
            case I18N_ROOM_FULL:
                return "房间已满\r\n";
            case I18N_INSERT_HINT_WIDE:
                return "Enter 发送 · Esc 浏览 · :support";
            case I18N_INSERT_HINT_NARROW:
                return "Enter · Esc · :support";
            case I18N_NORMAL_LATEST:
                return "G 最新";
            case I18N_NORMAL_NEW_MESSAGES:
                return "新消息";
        }
    }

    switch (id) {
        case I18N_USERNAME_PROMPT:
            return "  Enter display name (blank for anonymous): ";
        case I18N_INVALID_USERNAME:
            return "Invalid username. Using 'anonymous' instead.\r\n";
        case I18N_ROOM_FULL:
            return "Room is full\r\n";
        case I18N_INSERT_HINT_WIDE:
            return "Enter send · Esc browse · :support";
        case I18N_INSERT_HINT_NARROW:
            return "Enter · Esc · :support";
        case I18N_NORMAL_LATEST:
            return "G latest";
        case I18N_NORMAL_NEW_MESSAGES:
            return "new";
    }

    return "";
}
