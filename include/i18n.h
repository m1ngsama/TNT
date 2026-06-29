#ifndef I18N_H
#define I18N_H

#include "common.h"

typedef struct {
    const char *text[UI_LANG_COUNT];
} i18n_string_t;

#define I18N_LANG_TEXT(lang, value) [lang] = (value)
#define I18N_EN(value) I18N_LANG_TEXT(UI_LANG_EN, value)
#define I18N_ZH(value) I18N_LANG_TEXT(UI_LANG_ZH, value)
#define I18N_STRING_MAP(...) {{ __VA_ARGS__ }}
#define I18N_STRING(en_text, zh_text) \
    I18N_STRING_MAP(I18N_EN(en_text), I18N_ZH(zh_text))

typedef enum {
    I18N_USERNAME_PROMPT,
    I18N_INVALID_USERNAME,
    I18N_ROOM_FULL,
    I18N_WELCOME_SUBTITLE,
    I18N_WELCOME_TAGLINE,
    I18N_WELCOME_FALLBACK_FORMAT,
    I18N_INSERT_HINT_WIDE,
    I18N_INSERT_HINT_NARROW,
    I18N_NORMAL_LATEST,
    I18N_NORMAL_NEW_MESSAGES,
    I18N_HELP_TITLE,
    I18N_HELP_STATUS_FORMAT,
    I18N_COMMAND_OUTPUT_TITLE,
    I18N_COMMAND_OUTPUT_STATUS_FORMAT,
    I18N_COMMAND_OUTPUT_REFRESH_STATUS_FORMAT,
    I18N_MOTD_TITLE,
    I18N_MOTD_CONTINUE_HINT,
    I18N_TITLE_ONLINE_FORMAT,
    I18N_TITLE_MUTED,
    I18N_TITLE_HELP_HINT,
    I18N_EMPTY_ROOM,
    I18N_EMPTY_FILTERED,
    I18N_IDLE_TIMEOUT_FORMAT,
    I18N_SYSTEM_USERNAME,
    I18N_SYSTEM_JOIN_FORMAT,
    I18N_SYSTEM_LEAVE_FORMAT,
    I18N_SYSTEM_NICK_FORMAT,
    I18N_USERS_TITLE,
    I18N_MSG_SENT_FORMAT,
    I18N_MSG_USER_NOT_FOUND_FORMAT,
    I18N_REPLY_NO_TARGET,
    I18N_INBOX_TITLE,
    I18N_INBOX_EMPTY,
    I18N_INBOX_SENT_TO_FORMAT,
    I18N_INBOX_CLEARED,
    I18N_INBOX_UNREAD_FORMAT,
    I18N_NICK_INVALID,
    I18N_NICK_TAKEN_FORMAT,
    I18N_NICK_UNCHANGED,
    I18N_NICK_CHANGED_FORMAT,
    I18N_LAST_HEADER_FORMAT,
    I18N_LAST_EMPTY,
    I18N_SEARCH_HEADER_FORMAT,
    I18N_SEARCH_EMPTY,
    I18N_MUTE_JOINS_FORMAT,
    I18N_MUTE_JOINS_MUTED,
    I18N_MUTE_JOINS_UNMUTED,
    I18N_CLEAR_DONE,
    I18N_LANG_CURRENT_FORMAT,
    I18N_LANG_SET_FORMAT,
    I18N_LANG_UNSUPPORTED_FORMAT,
    I18N_THEME_CURRENT_FORMAT,
    I18N_THEME_SET_FORMAT,
    I18N_THEME_UNSUPPORTED_FORMAT,
    I18N_UNKNOWN_COMMAND_FORMAT,
    I18N_DID_YOU_MEAN_FORMAT,
    I18N_UNKNOWN_GUIDANCE,
    I18N_EXEC_POST_EMPTY,
    I18N_EXEC_POST_INVALID_UTF8,
    I18N_EXEC_POST_TOO_LONG,
    I18N_EXEC_POST_PERSIST_FAILED,
    I18N_EXEC_COMMAND_TOO_LONG,
    I18N_EXEC_UNKNOWN_COMMAND_FORMAT,
    I18N_TEXT_COUNT
} i18n_text_id_t;

bool i18n_try_parse_ui_lang(const char *value, ui_lang_t *lang);
ui_lang_t i18n_parse_ui_lang(const char *value, ui_lang_t fallback);
ui_lang_t i18n_default_ui_lang(void);
ui_lang_t i18n_next_ui_lang(ui_lang_t lang);
const char *i18n_ui_lang_code(ui_lang_t lang);
const char *i18n_text(ui_lang_t lang, i18n_text_id_t id);

static inline const char *i18n_string(i18n_string_t value, ui_lang_t lang) {
    if ((int)lang < 0 || lang >= UI_LANG_COUNT) {
        lang = UI_LANG_EN;
    }
    if (value.text[lang]) {
        return value.text[lang];
    }
    if (value.text[UI_LANG_EN]) {
        return value.text[UI_LANG_EN];
    }
    return "";
}

#endif /* I18N_H */
