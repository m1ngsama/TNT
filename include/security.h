#ifndef SECURITY_H
#define SECURITY_H

#include "common.h"

void security_audit(const char *event, const char *ip, const char *session,
                    const char *nickname, const char *reason);
int security_ban(const char *ip, int seconds, const char *reason);
int security_unban(const char *ip);
int security_list_bans(FILE *out);
int security_is_banned(const char *ip, time_t *until);
uint64_t security_ban_generation(void);

#endif
