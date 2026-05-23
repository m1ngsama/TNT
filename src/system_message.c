#include "system_message.h"
#include "i18n.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static void system_message_init(message_t *msg, help_lang_t lang) {
    if (!msg) {
        return;
    }

    memset(msg, 0, sizeof(*msg));
    msg->timestamp = time(NULL);
    snprintf(msg->username, sizeof(msg->username), "%s",
             i18n_text(lang, I18N_SYSTEM_USERNAME));
}

void system_message_make_join(message_t *msg, const char *username,
                              help_lang_t lang) {
    system_message_init(msg, lang);
    if (!msg) {
        return;
    }

    snprintf(msg->content, sizeof(msg->content),
             i18n_text(lang, I18N_SYSTEM_JOIN_FORMAT),
             username ? username : "");
}

void system_message_make_leave(message_t *msg, const char *username,
                               help_lang_t lang) {
    system_message_init(msg, lang);
    if (!msg) {
        return;
    }

    snprintf(msg->content, sizeof(msg->content),
             i18n_text(lang, I18N_SYSTEM_LEAVE_FORMAT),
             username ? username : "");
}

void system_message_make_nick(message_t *msg, const char *old_name,
                              const char *new_name, help_lang_t lang) {
    system_message_init(msg, lang);
    if (!msg) {
        return;
    }

    snprintf(msg->content, sizeof(msg->content),
             i18n_text(lang, I18N_SYSTEM_NICK_FORMAT),
             old_name ? old_name : "", new_name ? new_name : "");
}

bool system_message_is_system(const message_t *msg) {
    if (!msg) {
        return false;
    }

    return strcmp(msg->username, "系统") == 0 ||
           strcmp(msg->username, "system") == 0;
}

bool system_message_is_join_leave(const message_t *msg) {
    if (!system_message_is_system(msg)) {
        return false;
    }

    return strstr(msg->content, "加入了聊天室") != NULL ||
           strstr(msg->content, "离开了聊天室") != NULL ||
           strstr(msg->content, "joined the room") != NULL ||
           strstr(msg->content, "left the room") != NULL;
}
