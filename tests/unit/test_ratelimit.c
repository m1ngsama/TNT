/* Unit tests for connection and rate-limit accounting */

#include "../../include/ratelimit.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define TEST(name) static void test_##name()
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    test_##name(); \
    printf("✓\n"); \
    tests_passed++; \
} while(0)

static int tests_passed = 0;

TEST(per_ip_concurrent_limit_blocks_second_active_connection) {
    const char *ip = "203.0.113.10";

    setenv("TNT_RATE_LIMIT", "0", 1);
    setenv("TNT_MAX_CONN_PER_IP", "1", 1);
    ratelimit_init();

    assert(ratelimit_check_ip(ip) == true);
    assert(ratelimit_check_ip(ip) == false);

    ratelimit_release_ip(ip);
    assert(ratelimit_check_ip(ip) == true);
    ratelimit_release_ip(ip);
}

TEST(rate_limit_allows_configured_burst_then_blocks) {
    const char *ip = "203.0.113.20";

    setenv("TNT_RATE_LIMIT", "1", 1);
    setenv("TNT_MAX_CONN_PER_IP", "10", 1);
    setenv("TNT_MAX_CONN_RATE_PER_IP", "2", 1);
    ratelimit_init();

    assert(ratelimit_check_ip(ip) == true);
    ratelimit_release_ip(ip);
    assert(ratelimit_check_ip(ip) == true);
    ratelimit_release_ip(ip);
    assert(ratelimit_check_ip(ip) == false);
}

TEST(global_limit_tracks_active_total) {
    setenv("TNT_MAX_CONNECTIONS", "1", 1);
    ratelimit_init();

    assert(ratelimit_check_and_increment_total() == true);
    assert(ratelimit_get_active_total() == 1);
    assert(ratelimit_check_and_increment_total() == false);

    ratelimit_decrement_total();
    assert(ratelimit_get_active_total() == 0);
    assert(ratelimit_check_and_increment_total() == true);
    ratelimit_decrement_total();
}

int main(void) {
    printf("Running rate-limit unit tests...\n\n");

    RUN_TEST(per_ip_concurrent_limit_blocks_second_active_connection);
    RUN_TEST(rate_limit_allows_configured_burst_then_blocks);
    RUN_TEST(global_limit_tracks_active_total);

    printf("\n✓ All %d tests passed!\n", tests_passed);
    return 0;
}
