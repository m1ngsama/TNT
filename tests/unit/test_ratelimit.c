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

TEST(blocked_ip_survives_inactive_source_churn) {
    const char *blocked_ip = "192.0.2.200";
    char ip[32];

    setenv("TNT_RATE_LIMIT", "1", 1);
    setenv("TNT_MAX_CONNECTIONS", "64", 1);
    setenv("TNT_MAX_CONN_PER_IP", "2", 1);
    setenv("TNT_MAX_CONN_RATE_PER_IP", "1000", 1);
    ratelimit_init();

    for (int i = 0; i < 5; i++) {
        ratelimit_record_auth_failure(blocked_ip);
    }
    assert(ratelimit_check_ip(blocked_ip) == false);

    int capacity = ratelimit_test_capacity();
    assert(capacity >= 64 + 256);
    for (int i = 0; i < capacity + 32; i++) {
        snprintf(ip, sizeof(ip), "10.%d.%d.%d",
                 (i / (250 * 250)) % 250 + 1,
                 (i / 250) % 250 + 1, i % 250 + 1);
        assert(ratelimit_check_ip(ip) == true);
        ratelimit_release_ip(ip);
    }

    assert(ratelimit_test_auth_failures_for_ip(blocked_ip) == 5);
    assert(ratelimit_check_ip(blocked_ip) == false);
}

TEST(table_scales_past_legacy_256_ip_limit) {
    char ip[32];

    setenv("TNT_RATE_LIMIT", "0", 1);
    setenv("TNT_MAX_CONNECTIONS", "300", 1);
    setenv("TNT_MAX_CONN_PER_IP", "1", 1);
    ratelimit_init();

    assert(ratelimit_test_capacity() >= 300);
    for (int i = 0; i < 300; i++) {
        snprintf(ip, sizeof(ip), "198.51.%d.%d", i / 250, i % 250 + 1);
        assert(ratelimit_check_ip(ip) == true);
    }
    assert(ratelimit_test_active_for_ip("198.51.0.1") == 1);
    assert(ratelimit_test_active_for_ip("198.51.1.50") == 1);

    for (int i = 0; i < 300; i++) {
        snprintf(ip, sizeof(ip), "198.51.%d.%d", i / 250, i % 250 + 1);
        ratelimit_release_ip(ip);
    }
}

TEST(success_resets_consecutive_auth_failures) {
    const char *ip = "203.0.113.30";

    setenv("TNT_RATE_LIMIT", "1", 1);
    setenv("TNT_MAX_CONNECTIONS", "300", 1);
    ratelimit_init();

    assert(ratelimit_check_ip(ip) == true);
    ratelimit_record_auth_failure(ip);
    ratelimit_record_auth_failure(ip);
    assert(ratelimit_test_auth_failures_for_ip(ip) == 2);
    ratelimit_record_auth_success(ip);
    assert(ratelimit_test_auth_failures_for_ip(ip) == 0);
    ratelimit_release_ip(ip);
}

int main(void) {
    printf("Running rate-limit unit tests...\n\n");

    RUN_TEST(per_ip_concurrent_limit_blocks_second_active_connection);
    RUN_TEST(rate_limit_allows_configured_burst_then_blocks);
    RUN_TEST(global_limit_tracks_active_total);
    RUN_TEST(blocked_ip_survives_inactive_source_churn);
    RUN_TEST(table_scales_past_legacy_256_ip_limit);
    RUN_TEST(success_resets_consecutive_auth_failures);

    printf("\n✓ All %d tests passed!\n", tests_passed);
    return 0;
}
