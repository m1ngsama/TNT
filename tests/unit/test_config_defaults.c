#include "config_defaults.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    test_##name(); \
    printf("ok\n"); \
} while (0)

TEST(specs_expose_runtime_defaults) {
    assert(TNT_CONFIG_PORT.fallback == TNT_DEFAULT_PORT);
    assert(TNT_CONFIG_MAX_CONNECTIONS.fallback ==
           TNT_DEFAULT_MAX_CONNECTIONS);
    assert(TNT_CONFIG_MAX_CONN_PER_IP.fallback ==
           TNT_DEFAULT_MAX_CONN_PER_IP);
    assert(TNT_CONFIG_MAX_CONN_RATE_PER_IP.fallback ==
           TNT_DEFAULT_MAX_CONN_RATE_PER_IP);
    assert(TNT_CONFIG_RATE_LIMIT.fallback ==
           TNT_DEFAULT_RATE_LIMIT_ENABLED);
    assert(TNT_CONFIG_IDLE_TIMEOUT.fallback == TNT_DEFAULT_IDLE_TIMEOUT);
    assert(TNT_CONFIG_PORT.min_value == TNT_MIN_PORT);
    assert(TNT_CONFIG_PORT.max_value == TNT_MAX_PORT);
}

TEST(parse_uses_spec_ranges) {
    int out = 0;

    assert(tnt_config_parse_int("2222", &TNT_CONFIG_PORT, &out));
    assert(out == 2222);
    assert(!tnt_config_parse_int("0", &TNT_CONFIG_PORT, &out));
    assert(!tnt_config_parse_int("65536", &TNT_CONFIG_PORT, &out));
    assert(!tnt_config_parse_int("abc", &TNT_CONFIG_PORT, &out));
    assert(!tnt_config_parse_int("", &TNT_CONFIG_PORT, &out));

    assert(tnt_config_parse_int("0", &TNT_CONFIG_IDLE_TIMEOUT, &out));
    assert(out == 0);
    assert(!tnt_config_parse_int("86401", &TNT_CONFIG_IDLE_TIMEOUT, &out));
}

TEST(env_reader_uses_fallback_and_range) {
    unsetenv(TNT_CONFIG_MAX_CONNECTIONS.env_name);
    assert(tnt_config_env_int(&TNT_CONFIG_MAX_CONNECTIONS) ==
           TNT_DEFAULT_MAX_CONNECTIONS);

    setenv(TNT_CONFIG_MAX_CONNECTIONS.env_name, "128", 1);
    assert(tnt_config_env_int(&TNT_CONFIG_MAX_CONNECTIONS) == 128);

    setenv(TNT_CONFIG_MAX_CONNECTIONS.env_name, "0", 1);
    assert(tnt_config_env_int(&TNT_CONFIG_MAX_CONNECTIONS) ==
           TNT_DEFAULT_MAX_CONNECTIONS);

    unsetenv(TNT_CONFIG_MAX_CONNECTIONS.env_name);
}

int main(void) {
    printf("Running config defaults unit tests...\n\n");
    RUN_TEST(specs_expose_runtime_defaults);
    RUN_TEST(parse_uses_spec_ranges);
    RUN_TEST(env_reader_uses_fallback_and_range);
    printf("\nAll 3 tests passed!\n");
    return 0;
}
