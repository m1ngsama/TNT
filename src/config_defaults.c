#include "config_defaults.h"
#include "common.h"

#include <stdlib.h>

const tnt_int_config_spec_t TNT_CONFIG_PORT = {
    "PORT",
    TNT_DEFAULT_PORT,
    TNT_MIN_PORT,
    TNT_MAX_PORT,
};

const tnt_int_config_spec_t TNT_CONFIG_MAX_CONNECTIONS = {
    "TNT_MAX_CONNECTIONS",
    TNT_DEFAULT_MAX_CONNECTIONS,
    TNT_MIN_CONFIGURED_CLIENTS,
    TNT_MAX_CONFIGURED_CLIENTS,
};

const tnt_int_config_spec_t TNT_CONFIG_MAX_CONN_PER_IP = {
    "TNT_MAX_CONN_PER_IP",
    TNT_DEFAULT_MAX_CONN_PER_IP,
    TNT_MIN_CONFIGURED_CLIENTS,
    TNT_MAX_CONFIGURED_CLIENTS,
};

const tnt_int_config_spec_t TNT_CONFIG_MAX_CONN_RATE_PER_IP = {
    "TNT_MAX_CONN_RATE_PER_IP",
    TNT_DEFAULT_MAX_CONN_RATE_PER_IP,
    TNT_MIN_CONFIGURED_CLIENTS,
    TNT_MAX_CONFIGURED_CLIENTS,
};

const tnt_int_config_spec_t TNT_CONFIG_RATE_LIMIT = {
    "TNT_RATE_LIMIT",
    TNT_DEFAULT_RATE_LIMIT_ENABLED,
    TNT_MIN_RATE_LIMIT_ENABLED,
    TNT_MAX_RATE_LIMIT_ENABLED,
};

const tnt_int_config_spec_t TNT_CONFIG_IDLE_TIMEOUT = {
    "TNT_IDLE_TIMEOUT",
    TNT_DEFAULT_IDLE_TIMEOUT,
    TNT_MIN_IDLE_TIMEOUT,
    TNT_MAX_IDLE_TIMEOUT,
};

const tnt_int_config_spec_t TNT_CONFIG_SSH_LOG_LEVEL = {
    "TNT_SSH_LOG_LEVEL",
    0,
    TNT_MIN_SSH_LOG_LEVEL,
    TNT_MAX_SSH_LOG_LEVEL,
};

int tnt_config_env_int(const tnt_int_config_spec_t *spec) {
    if (!spec) {
        return 0;
    }
    return env_int(spec->env_name, spec->fallback, spec->min_value,
                   spec->max_value);
}

bool tnt_config_parse_int(const char *value, const tnt_int_config_spec_t *spec,
                          int *out) {
    char *end = NULL;
    long val;

    if (!value || value[0] == '\0' || !spec || !out) {
        return false;
    }

    val = strtol(value, &end, 10);
    if (!end || *end != '\0' || val < spec->min_value ||
        val > spec->max_value) {
        return false;
    }

    *out = (int)val;
    return true;
}
