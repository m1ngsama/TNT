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
    editor_move_home(&ed, 20);
    assert(!editor_move_left(&ed));
    assert(editor_cursor(&ed) == 0);
    editor_move_end(&ed, 20);
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
    editor_move_home(&ed, 20);
    assert(editor_cursor_column(&ed) == 0);
    assert(editor_move_right(&ed));
    assert(editor_cursor_column(&ed) == 2);
}

TEST(editor_inserts_a_newline) {
    editor_t ed;

    editor_reset(&ed);
    assert(editor_insert_bytes(&ed, "ab", 2));
    assert(editor_insert_newline(&ed));
    assert(editor_insert_bytes(&ed, "cd", 2));
    assert(strcmp(editor_text(&ed), "ab\ncd") == 0);
    assert(editor_cursor(&ed) == 5);
}

TEST(editor_counts_display_rows) {
    editor_t ed;

    editor_reset(&ed);
    assert(editor_set_text(&ed, ""));
    assert(editor_display_rows(&ed, 20) == 1);   /* empty is still one row */

    assert(editor_set_text(&ed, "one\ntwo\nthree"));
    assert(editor_display_rows(&ed, 20) == 3);   /* hard breaks */

    assert(editor_set_text(&ed, "aaaaaaaaaa"));
    assert(editor_display_rows(&ed, 4) == 3);    /* soft wrap: 4+4+2 */
}

TEST(editor_reports_the_caret_row_and_column) {
    editor_t ed;

    editor_reset(&ed);
    assert(editor_set_text(&ed, "one\ntwo"));    /* cursor at the end */
    assert(editor_caret_row(&ed, 20) == 1);
    assert(editor_caret_column(&ed, 20) == 3);

    editor_move_home(&ed, 20);
    assert(editor_caret_row(&ed, 20) == 1);
    assert(editor_caret_column(&ed, 20) == 0);
    assert(editor_cursor(&ed) == 4);             /* after the newline */
}

TEST(editor_moves_between_display_lines) {
    editor_t ed;

    editor_reset(&ed);
    assert(editor_set_text(&ed, "abcd\nefgh"));  /* cursor at the end */
    assert(editor_move_up(&ed, 20));
    assert(editor_caret_row(&ed, 20) == 0);
    assert(editor_caret_column(&ed, 20) == 4);   /* column is preserved */

    assert(!editor_move_up(&ed, 20));            /* already on the first row */

    assert(editor_move_down(&ed, 20));
    assert(editor_caret_row(&ed, 20) == 1);
    assert(!editor_move_down(&ed, 20));          /* already on the last row */
}

TEST(editor_up_clamps_to_a_shorter_row) {
    editor_t ed;

    editor_reset(&ed);
    assert(editor_set_text(&ed, "ab\nefgh"));    /* cursor at the end, col 4 */
    assert(editor_move_up(&ed, 20));
    assert(editor_caret_row(&ed, 20) == 0);
    assert(editor_caret_column(&ed, 20) == 2);   /* clamped to the row's end */
    assert(editor_cursor(&ed) == 2);
}

TEST(editor_home_and_end_are_scoped_to_the_display_row) {
    editor_t ed;

    editor_reset(&ed);
    assert(editor_set_text(&ed, "one\ntwo"));
    editor_move_home(&ed, 20);
    assert(editor_cursor(&ed) == 4);
    editor_move_end(&ed, 20);
    assert(editor_cursor(&ed) == 7);

    assert(editor_move_up(&ed, 20));
    editor_move_end(&ed, 20);
    assert(editor_cursor(&ed) == 3);             /* end of "one", not the buffer */
}

TEST(editor_navigation_keeps_cluster_boundaries) {
    editor_t ed;

    editor_reset(&ed);
    assert(editor_set_text(&ed, "😌😌\nab"));
    assert(editor_move_up(&ed, 20));
    /* The caret must land on a cluster start, never inside the emoji. */
    assert(editor_cursor(&ed) == 0 || editor_cursor(&ed) == 4 ||
           editor_cursor(&ed) == 8);
}

TEST(editor_rows_are_bounded_by_the_wrap_table) {
    editor_t ed;
    char many[MAX_MESSAGE_LEN];
    size_t i;

    editor_reset(&ed);
    for (i = 0; i + 1 < sizeof(many); i++) {
        many[i] = (i % 2) ? '\n' : 'x';
    }
    many[sizeof(many) - 1] = '\0';
    assert(editor_set_text(&ed, many));
    /* More logical lines than the wrap table holds must not overrun it. */
    assert(editor_display_rows(&ed, 20) >= 1);
    assert(editor_display_rows(&ed, 20) <= EDITOR_ROW_TABLE);
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
    RUN_TEST(editor_inserts_a_newline);
    RUN_TEST(editor_counts_display_rows);
    RUN_TEST(editor_reports_the_caret_row_and_column);
    RUN_TEST(editor_moves_between_display_lines);
    RUN_TEST(editor_up_clamps_to_a_shorter_row);
    RUN_TEST(editor_home_and_end_are_scoped_to_the_display_row);
    RUN_TEST(editor_navigation_keeps_cluster_boundaries);
    RUN_TEST(editor_rows_are_bounded_by_the_wrap_table);
    printf("\nAll %d editor tests passed!\n", tests_passed);
    return 0;
}
