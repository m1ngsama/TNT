/* Unit tests for the cursor-aware input editor */
#include "../../include/editor.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define TEST(name) static void test_##name()
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    test_##name(); \
    printf("✓\n"); \
    tests_passed++; \
} while(0)

static int tests_passed = 0;

TEST(editor_inserts_at_the_cursor) {
    editor_t ed;
    editor_reset(&ed);
    assert(editor_insert_bytes(&ed, "helo", 4));
    assert(editor_move_left(&ed));
    assert(editor_insert_bytes(&ed, "l", 1));
    assert(strcmp(editor_text(&ed), "hello") == 0);
    assert(editor_cursor(&ed) == 4);
}

TEST(editor_deletes_before_and_after_the_cursor) {
    editor_t ed;
    editor_reset(&ed);
    assert(editor_insert_bytes(&ed, "abcd", 4));
    assert(editor_move_left(&ed));
    assert(editor_delete_prev_cluster(&ed));   /* removes 'c' */
    assert(strcmp(editor_text(&ed), "abd") == 0);
    assert(editor_delete_next_cluster(&ed));   /* removes 'd' */
    assert(strcmp(editor_text(&ed), "ab") == 0);
}

TEST(editor_cursor_stays_on_cluster_boundaries) {
    editor_t ed;
    editor_reset(&ed);
    /* a + flag + b: moving left twice must land between 'a' and the flag,
     * never inside the flag's two regional indicators. */
    assert(editor_set_text(&ed, "a🇨🇳b"));
    assert(editor_move_left(&ed));
    assert(editor_move_left(&ed));
    assert(editor_cursor(&ed) == 1);
    assert(editor_cursor_column(&ed) == 1);
}

TEST(editor_backspace_removes_a_whole_emoji) {
    editor_t ed;
    editor_reset(&ed);
    assert(editor_set_text(&ed, "hi👨\xE2\x80\x8D👩"));
    assert(editor_delete_prev_cluster(&ed));
    assert(strcmp(editor_text(&ed), "hi") == 0);
}

TEST(editor_moves_stop_at_the_bounds) {
    editor_t ed;
    editor_reset(&ed);
    assert(editor_set_text(&ed, "ab"));
    editor_move_home(&ed);
    assert(!editor_move_left(&ed));
    assert(editor_cursor(&ed) == 0);
    editor_move_end(&ed);
    assert(!editor_move_right(&ed));
    assert(editor_cursor(&ed) == 2);
}

TEST(editor_word_operations) {
    editor_t ed;
    editor_reset(&ed);
    assert(editor_set_text(&ed, "one two three"));
    assert(editor_move_prev_word(&ed));
    assert(editor_cursor(&ed) == 8);
    assert(editor_delete_prev_word(&ed));
    assert(strcmp(editor_text(&ed), "one three") == 0);
}

TEST(editor_rejects_overflow_without_corrupting) {
    editor_t ed;
    char big[MAX_MESSAGE_LEN];
    editor_reset(&ed);
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';

    /* 1023 bytes is exactly the documented content limit, so it fits. */
    assert(editor_insert_bytes(&ed, big, strlen(big)));
    assert(editor_len(&ed) == MAX_MESSAGE_LEN - 1);

    /* One more byte overflows, and must leave the buffer untouched. */
    assert(editor_insert_bytes(&ed, "y", 1) == false);
    assert(editor_len(&ed) == MAX_MESSAGE_LEN - 1);
    assert(strcmp(editor_text(&ed), big) == 0);
}

TEST(editor_cursor_column_counts_display_width) {
    editor_t ed;
    editor_reset(&ed);
    assert(editor_set_text(&ed, "中文a"));
    editor_move_home(&ed);
    assert(editor_cursor_column(&ed) == 0);
    assert(editor_move_right(&ed));
    assert(editor_cursor_column(&ed) == 2);
}

int main(void) {
    printf("Running editor unit tests...\n\n");
    RUN_TEST(editor_inserts_at_the_cursor);
    RUN_TEST(editor_deletes_before_and_after_the_cursor);
    RUN_TEST(editor_cursor_stays_on_cluster_boundaries);
    RUN_TEST(editor_backspace_removes_a_whole_emoji);
    RUN_TEST(editor_moves_stop_at_the_bounds);
    RUN_TEST(editor_word_operations);
    RUN_TEST(editor_rejects_overflow_without_corrupting);
    RUN_TEST(editor_cursor_column_counts_display_width);
    printf("\nAll %d editor tests passed!\n", tests_passed);
    return 0;
}
