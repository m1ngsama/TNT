#ifndef RATELIMIT_H
#define RATELIMIT_H

#include <stdbool.h>

/* Read TNT_MAX_CONNECTIONS / TNT_MAX_CONN_PER_IP / TNT_MAX_CONN_RATE_PER_IP /
 * TNT_RATE_LIMIT from the environment.  Idempotent, call once at startup. */
void ratelimit_init(void);

/* Per-IP entry point: returns false if the IP has hit any limit (concurrent,
 * rate, or block).  On success, increments the IP's active counter — caller
 * MUST pair with ratelimit_release_ip() when the connection ends. */
bool ratelimit_check_ip(const char *ip);
void ratelimit_release_ip(const char *ip);

/* Auth-failure ledger.  After enough failures within the window the IP is
 * blocked for a fixed duration. */
void ratelimit_record_auth_failure(const char *ip);

/* Global active-connection cap (separate from per-IP).  Pair them. */
bool ratelimit_check_and_increment_total(void);
void ratelimit_decrement_total(void);

/* Read-only accessor for stats subcommand. */
int  ratelimit_get_active_total(void);

#endif /* RATELIMIT_H */
