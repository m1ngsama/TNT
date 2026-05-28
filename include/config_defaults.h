#ifndef CONFIG_DEFAULTS_H
#define CONFIG_DEFAULTS_H

#include <stdbool.h>

#define TNT_STRINGIFY_VALUE(value) #value
#define TNT_STRINGIFY(value) TNT_STRINGIFY_VALUE(value)

#define TNT_DEFAULT_PORT 2222
#define TNT_DEFAULT_PORT_TEXT TNT_STRINGIFY(TNT_DEFAULT_PORT)
#define TNT_DEFAULT_MAX_CONNECTIONS 64
#define TNT_DEFAULT_MAX_CONN_PER_IP 5
#define TNT_DEFAULT_MAX_CONN_RATE_PER_IP 10
#define TNT_DEFAULT_RATE_LIMIT_ENABLED 1
#define TNT_DEFAULT_IDLE_TIMEOUT 1800

#define TNT_MIN_PORT 1
#define TNT_MAX_PORT 65535
#define TNT_MIN_CONFIGURED_CLIENTS 1
#define TNT_MAX_CONFIGURED_CLIENTS 1024
#define TNT_MIN_RATE_LIMIT_ENABLED 0
#define TNT_MAX_RATE_LIMIT_ENABLED 1
#define TNT_MIN_IDLE_TIMEOUT 0
#define TNT_MAX_IDLE_TIMEOUT 86400
#define TNT_MIN_SSH_LOG_LEVEL 0
#define TNT_MAX_SSH_LOG_LEVEL 4

typedef struct {
    const char *env_name;
    int fallback;
    int min_value;
    int max_value;
} tnt_int_config_spec_t;

extern const tnt_int_config_spec_t TNT_CONFIG_PORT;
extern const tnt_int_config_spec_t TNT_CONFIG_MAX_CONNECTIONS;
extern const tnt_int_config_spec_t TNT_CONFIG_MAX_CONN_PER_IP;
extern const tnt_int_config_spec_t TNT_CONFIG_MAX_CONN_RATE_PER_IP;
extern const tnt_int_config_spec_t TNT_CONFIG_RATE_LIMIT;
extern const tnt_int_config_spec_t TNT_CONFIG_IDLE_TIMEOUT;
extern const tnt_int_config_spec_t TNT_CONFIG_SSH_LOG_LEVEL;

int tnt_config_env_int(const tnt_int_config_spec_t *spec);
bool tnt_config_parse_int(const char *value, const tnt_int_config_spec_t *spec,
                          int *out);

#endif /* CONFIG_DEFAULTS_H */
