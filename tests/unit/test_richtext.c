/* Unit tests for width-aware wrapping */
#include "../../include/richtext.h"
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

static int span_equals(const char *text, richtext_span_t span,
                       const char *expected) {
    size_t len = strlen(expected);
    return span.len == len && memcmp(text + span.offset, expected, len) == 0;
}

TEST(wrap_short_text_is_one_line) {
    richtext_span_t spans[4];
    const char *text = "hello";

    assert(richtext_wrap(text, 20, spans, 4) == 1);
    assert(span_equals(text, spans[0], "hello"));
}

TEST(wrap_breaks_at_a_space) {
    richtext_span_t spans[4];
    const char *text = "hello world";

    assert(richtext_wrap(text, 7, spans, 4) == 2);
    assert(span_equals(text, spans[0], "hello"));
    assert(span_equals(text, spans[1], "world"));
}

TEST(wrap_hard_breaks_when_no_space_fits) {
    richtext_span_t spans[4];
    const char *text = "abcdefgh";

    assert(richtext_wrap(text, 3, spans, 4) == 3);
    assert(span_equals(text, spans[0], "abc"));
    assert(span_equals(text, spans[1], "def"));
    assert(span_equals(text, spans[2], "gh"));
}

TEST(wrap_never_splits_a_wide_character) {
    richtext_span_t spans[4];
    const char *text = "中文中文";

    /* Three columns hold one double-width character, not one and a half. */
    assert(richtext_wrap(text, 3, spans, 4) == 4);
    assert(span_equals(text, spans[0], "中"));
}

TEST(wrap_never_splits_an_emoji_cluster) {
    richtext_span_t spans[4];
    const char *text = "🇨🇳🇯🇵";

    assert(richtext_wrap(text, 3, spans, 4) == 2);
    assert(span_equals(text, spans[0], "🇨🇳"));
}

TEST(wrap_treats_newline_as_a_hard_break) {
    richtext_span_t spans[4];
    const char *text = "ab\ncd";

    assert(richtext_wrap(text, 20, spans, 4) == 2);
    assert(span_equals(text, spans[0], "ab"));
    assert(span_equals(text, spans[1], "cd"));
}

TEST(wrap_rejects_bad_arguments) {
    richtext_span_t spans[4];

    assert(richtext_wrap(NULL, 10, spans, 4) == 0);
    assert(richtext_wrap("x", 0, spans, 4) == 0);
    assert(richtext_wrap("x", 10, spans, 0) == 0);
}

int main(void) {
    printf("Running richtext unit tests...\n\n");
    RUN_TEST(wrap_short_text_is_one_line);
    RUN_TEST(wrap_breaks_at_a_space);
    RUN_TEST(wrap_hard_breaks_when_no_space_fits);
    RUN_TEST(wrap_never_splits_a_wide_character);
    RUN_TEST(wrap_never_splits_an_emoji_cluster);
    RUN_TEST(wrap_treats_newline_as_a_hard_break);
    RUN_TEST(wrap_rejects_bad_arguments);
    printf("\nAll %d richtext tests passed!\n", tests_passed);
    return 0;
}
