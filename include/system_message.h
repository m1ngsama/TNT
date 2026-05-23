#ifndef SYSTEM_MESSAGE_H
#define SYSTEM_MESSAGE_H

#include "common.h"
#include "message.h"

void system_message_make_join(message_t *msg, const char *username,
                              help_lang_t lang);
void system_message_make_leave(message_t *msg, const char *username,
                               help_lang_t lang);
void system_message_make_nick(message_t *msg, const char *old_name,
                              const char *new_name, help_lang_t lang);

bool system_message_is_system(const message_t *msg);
bool system_message_is_join_leave(const message_t *msg);

#endif /* SYSTEM_MESSAGE_H */
