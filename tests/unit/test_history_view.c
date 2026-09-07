/* Unit tests for history_view viewport and scroll rules */

#include "../../include/history_view.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TEST(name) static void test_##name()
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    test_##name(); \
    printf("✓\n"); \
    tests_passed++; \
} while(0)

static int tests_passed = 0;

static message_t make_msg(time_t timestamp, const char *content) {
    message_t msg = { .timestamp = timestamp };
    snprintf(msg.username, sizeof(msg.username), "user");
    snprintf(msg.content, sizeof(msg.content), "%s", content);
    return msg;
}

TEST(height_clamps_to_message_area) {
    assert(history_view_height(24) == 21);
    assert(history_view_height(4) == 1);
    assert(history_view_height(1) == 1);
    assert(history_view_height(0) == 1);
}

TEST(max_scroll_clamps_to_zero) {
    assert(history_view_max_scroll(0, 20) == 0);
    assert(history_view_max_scroll(10, 20) == 0);
    assert(history_view_max_scroll(20, 20) == 0);
    assert(history_view_max_scroll(25, 20) == 5);
}

TEST(scroll_to_latest_enables_follow_tail) {
    int scroll = 0;
    bool follow = false;

    history_view_scroll_to_latest(&scroll, &follow, 30, 10);
    assert(scroll == 20);
    assert(follow == true);
}

TEST(scroll_to_oldest_disables_follow_tail) {
    int scroll = 12;
    bool follow = true;

    history_view_scroll_to_oldest(&scroll, &follow);
    assert(scroll == 0);
    assert(follow == false);
}

TEST(scroll_by_clamps_and_toggles_follow) {
    int scroll = 20;
    bool follow = true;

    history_view_scroll_by(&scroll, &follow, 30, 10, -3);
    assert(scroll == 17);
    assert(follow == false);

    history_view_scroll_by(&scroll, &follow, 30, 10, 100);
    assert(scroll == 20);
    assert(follow == true);

    history_view_scroll_by(&scroll, &follow, 30, 10, -100);
    assert(scroll == 0);
    assert(follow == false);
}

TEST(latest_start_counts_date_dividers) {
    message_t messages[6];
    messages[0] = make_msg(1704067200, "day1-1");  /* 2024-01-01 */
    messages[1] = make_msg(1704067260, "day1-2");
    messages[2] = make_msg(1704153600, "day2-1");  /* 2024-01-02 */
    messages[3] = make_msg(1704153660, "day2-2");
    messages[4] = make_msg(1704240000, "day3-1");  /* 2024-01-03 */
    messages[5] = make_msg(1704240060, "day3-2");

    assert(history_view_latest_start_for_height(messages, 6, 3) == 4);
    assert(history_view_latest_start_for_height(messages, 6, 4) == 4);
    assert(history_view_latest_start_for_height(messages, 6, 5) == 3);
    assert(history_view_latest_start_for_height(messages, 6, 6) == 2);
}

TEST(latest_start_handles_empty_and_tiny_view) {
    message_t messages[1];
    messages[0] = make_msg(1704067200, "only");

    assert(history_view_latest_start_for_height(messages, 0, 3) == 0);
    assert(history_view_latest_start_for_height(messages, 1, 1) == 0);
}

TEST(latest_start_uses_prepared_date_cache) {
    message_t messages[4];
    for (int i = 0; i < 4; i++) {
        messages[i] = make_msg(0, "cached");
    }
    snprintf(messages[0].display_date, sizeof(messages[0].display_date),
             "2024-01-01");
    snprintf(messages[1].display_date, sizeof(messages[1].display_date),
             "2024-01-01");
    snprintf(messages[2].display_date, sizeof(messages[2].display_date),
             "2024-01-02");
    snprintf(messages[3].display_date, sizeof(messages[3].display_date),
             "2024-01-02");

    assert(history_view_latest_start_for_height(messages, 4, 3) == 2);
    assert(history_view_latest_start_for_height(messages, 4, 4) == 2);
    assert(history_view_latest_start_for_height(messages, 4, 5) == 1);
}

TEST(history_view_counts_wrapped_message_rows) {
    message_t msg = {0};

    snprintf(msg.username, sizeof(msg.username), "u");
    snprintf(msg.content, sizeof(msg.content),
             "aaaaaaaaaa bbbbbbbbbb cccccccccc");

    /* Wide enough for one row. */
    assert(history_view_message_lines(&msg, 200) == 1);
    /* Narrow enough that the content needs more than one row. */
    assert(history_view_message_lines(&msg, 20) > 1);
}

TEST(history_view_message_lines_minimum_is_one) {
    message_t msg = {0};

    snprintf(msg.username, sizeof(msg.username), "u");
    snprintf(msg.content, sizeof(msg.content), "x");
    assert(history_view_message_lines(&msg, 1) == 1);
    assert(history_view_message_lines(&msg, 0) == 1);
}

int main(void) {
    printf("=== History View Unit Tests ===\n");

    RUN_TEST(height_clamps_to_message_area);
    RUN_TEST(max_scroll_clamps_to_zero);
    RUN_TEST(scroll_to_latest_enables_follow_tail);
    RUN_TEST(scroll_to_oldest_disables_follow_tail);
    RUN_TEST(scroll_by_clamps_and_toggles_follow);
    RUN_TEST(latest_start_counts_date_dividers);
    RUN_TEST(latest_start_handles_empty_and_tiny_view);
    RUN_TEST(latest_start_uses_prepared_date_cache);
    RUN_TEST(history_view_counts_wrapped_message_rows);
    RUN_TEST(history_view_message_lines_minimum_is_one);

    printf("\nAll %d tests passed!\n", tests_passed);
    return 0;
}
