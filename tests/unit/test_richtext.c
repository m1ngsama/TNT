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


static size_t parse(const char *in, char *out, size_t out_size, size_t *len,
                    richtext_run_t *runs, size_t max_runs) {
    return richtext_parse(in, out, out_size, len, runs, max_runs);
}

static int run_is(const char *visible, richtext_run_t run,
                  richtext_style_t style, const char *expected) {
    size_t len = strlen(expected);
    return run.style == style && run.len == len &&
           memcmp(visible + run.offset, expected, len) == 0;
}

TEST(parse_plain_text_has_no_runs) {
    char out[64];
    size_t len;
    richtext_run_t runs[8];

    assert(parse("hello world", out, sizeof(out), &len, runs, 8) == 0);
    assert(strcmp(out, "hello world") == 0);
    assert(len == 11);
}

TEST(parse_strips_bold_markers) {
    char out[64];
    size_t len;
    richtext_run_t runs[8];

    assert(parse("a **big** deal", out, sizeof(out), &len, runs, 8) == 1);
    assert(strcmp(out, "a big deal") == 0);
    assert(run_is(out, runs[0], RICHTEXT_BOLD, "big"));
}

TEST(parse_strips_inline_code_markers) {
    char out[64];
    size_t len;
    richtext_run_t runs[8];

    assert(parse("run `make test` now", out, sizeof(out), &len, runs, 8) == 1);
    assert(strcmp(out, "run make test now") == 0);
    assert(run_is(out, runs[0], RICHTEXT_CODE, "make test"));
}

TEST(parse_leaves_an_unclosed_marker_literal) {
    char out[64];
    size_t len;
    richtext_run_t runs[8];

    assert(parse("**never closed", out, sizeof(out), &len, runs, 8) == 0);
    assert(strcmp(out, "**never closed") == 0);

    assert(parse("a `dangling", out, sizeof(out), &len, runs, 8) == 0);
    assert(strcmp(out, "a `dangling") == 0);
}

TEST(parse_leaves_empty_markers_literal) {
    char out[64];
    size_t len;
    richtext_run_t runs[8];

    assert(parse("****", out, sizeof(out), &len, runs, 8) == 0);
    assert(strcmp(out, "****") == 0);

    assert(parse("``", out, sizeof(out), &len, runs, 8) == 0);
    assert(strcmp(out, "``") == 0);
}

TEST(parse_does_not_read_bold_inside_code) {
    char out[64];
    size_t len;
    richtext_run_t runs[8];

    assert(parse("`a **b** c`", out, sizeof(out), &len, runs, 8) == 1);
    assert(strcmp(out, "a **b** c") == 0);
    assert(run_is(out, runs[0], RICHTEXT_CODE, "a **b** c"));
}

TEST(parse_marks_a_fenced_block_as_code) {
    char out[128];
    size_t len;
    richtext_run_t runs[8];

    assert(parse("before\n```\nx = 1\ny = 2\n```\nafter", out, sizeof(out),
                 &len, runs, 8) == 1);
    assert(strcmp(out, "before\nx = 1\ny = 2\nafter") == 0);
    assert(run_is(out, runs[0], RICHTEXT_CODE, "x = 1\ny = 2"));
}

TEST(parse_leaves_an_unclosed_fence_literal) {
    char out[128];
    size_t len;
    richtext_run_t runs[8];

    assert(parse("```\nstill open", out, sizeof(out), &len, runs, 8) == 0);
    assert(strcmp(out, "```\nstill open") == 0);
}

TEST(parse_highlights_a_bare_url) {
    char out[128];
    size_t len;
    richtext_run_t runs[8];

    assert(parse("see https://tnt.example/x now", out, sizeof(out), &len,
                 runs, 8) == 1);
    assert(strcmp(out, "see https://tnt.example/x now") == 0);
    assert(run_is(out, runs[0], RICHTEXT_URL, "https://tnt.example/x"));
}

TEST(parse_does_not_highlight_a_url_inside_code) {
    char out[128];
    size_t len;
    richtext_run_t runs[8];

    assert(parse("`https://x.example`", out, sizeof(out), &len, runs, 8) == 1);
    assert(run_is(out, runs[0], RICHTEXT_CODE, "https://x.example"));
}

TEST(parse_keeps_runs_ordered_and_disjoint) {
    char out[128];
    size_t len;
    richtext_run_t runs[8];
    size_t count = parse("**a** plain `b` https://c.example", out, sizeof(out),
                         &len, runs, 8);

    assert(count == 3);
    assert(runs[0].style == RICHTEXT_BOLD);
    assert(runs[1].style == RICHTEXT_CODE);
    assert(runs[2].style == RICHTEXT_URL);
    for (size_t i = 1; i < count; i++) {
        assert(runs[i].offset >= runs[i - 1].offset + runs[i - 1].len);
    }
}

TEST(parse_wrapping_measures_the_visible_text) {
    char out[64];
    size_t len;
    richtext_run_t runs[8];
    richtext_span_t spans[4];

    /* The markers must not count towards the width, or the row the renderer
     * draws and the row count the history uses disagree. */
    parse("**abcde**", out, sizeof(out), &len, runs, 8);
    assert(richtext_wrap(out, 5, spans, 4) == 1);
}

TEST(parse_without_a_run_list_strips_the_same_markers) {
    char with[64];
    char without[64];
    size_t a, b;
    richtext_run_t runs[8];

    /* The row count asks for no runs and the renderer asks for runs; if the
     * two stripped differently they would disagree on where a line breaks,
     * which is the bug PR #79 fixed. */
    parse("**a** `b` c", with, sizeof(with), &a, runs, 8);
    parse("**a** `b` c", without, sizeof(without), &b, NULL, 8);
    assert(a == b && strcmp(with, without) == 0);
}

TEST(parse_full_run_table_keeps_markers_literal) {
    char out[64];
    size_t len;
    richtext_run_t runs[1];

    /* The second pair has nowhere to go, so it must survive as text rather
     * than lose its markers and its styling both. */
    assert(parse("**a** **b**", out, sizeof(out), &len, runs, 1) == 1);
    assert(strcmp(out, "a **b**") == 0);
}

TEST(parse_rejects_bad_arguments) {
    char out[8];
    size_t len;
    richtext_run_t runs[4];

    assert(parse(NULL, out, sizeof(out), &len, runs, 4) == 0);
    assert(parse("x", NULL, sizeof(out), &len, runs, 4) == 0);
    assert(parse("x", out, 0, &len, runs, 4) == 0);
}

TEST(parse_truncates_rather_than_overflowing) {
    char out[8];
    size_t len;
    richtext_run_t runs[4];

    parse("**abcdefghijklmnop**", out, sizeof(out), &len, runs, 4);
    assert(len < sizeof(out));
    assert(out[len] == '\0');
}

static const char *styled(const char *text, size_t from, size_t to,
                          const char *resume) {
    static char out[256];
    char visible[128];
    size_t len;
    size_t pos = 0;
    richtext_run_t runs[8];
    size_t count = parse(text, visible, sizeof(visible), &len, runs, 8);

    out[0] = '\0';
    richtext_append_styled(out, sizeof(out), &pos, visible, from,
                           to ? to : len, runs, count, resume);
    return out;
}

TEST(append_wraps_a_run_in_its_sgr_pair) {
    assert(strcmp(styled("a **b** c", 0, 0, ""),
                  "a \033[1mb\033[22m c") == 0);
}

TEST(append_resumes_the_surrounding_style) {
    assert(strcmp(styled("a **b** c", 0, 0, "\033[33m"),
                  "a \033[1mb\033[22m\033[33m c") == 0);
}

TEST(append_reopens_a_run_cut_by_the_range) {
    assert(strcmp(styled("x `abcd` y", 3, 8, ""),
                  "\033[36mbcd\033[39m y") == 0);
}

TEST(append_closes_a_run_at_each_newline) {
    assert(strcmp(styled("```\nab\n\ncd\n```", 0, 0, ""),
                  "\033[36mab\033[39m\n\n\033[36mcd\033[39m") == 0);
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
    RUN_TEST(parse_plain_text_has_no_runs);
    RUN_TEST(parse_strips_bold_markers);
    RUN_TEST(parse_strips_inline_code_markers);
    RUN_TEST(parse_leaves_an_unclosed_marker_literal);
    RUN_TEST(parse_leaves_empty_markers_literal);
    RUN_TEST(parse_does_not_read_bold_inside_code);
    RUN_TEST(parse_marks_a_fenced_block_as_code);
    RUN_TEST(parse_leaves_an_unclosed_fence_literal);
    RUN_TEST(parse_highlights_a_bare_url);
    RUN_TEST(parse_does_not_highlight_a_url_inside_code);
    RUN_TEST(parse_keeps_runs_ordered_and_disjoint);
    RUN_TEST(parse_wrapping_measures_the_visible_text);
    RUN_TEST(parse_without_a_run_list_strips_the_same_markers);
    RUN_TEST(parse_full_run_table_keeps_markers_literal);
    RUN_TEST(parse_rejects_bad_arguments);
    RUN_TEST(parse_truncates_rather_than_overflowing);
    RUN_TEST(append_wraps_a_run_in_its_sgr_pair);
    RUN_TEST(append_resumes_the_surrounding_style);
    RUN_TEST(append_reopens_a_run_cut_by_the_range);
    RUN_TEST(append_closes_a_run_at_each_newline);
    printf("\nAll %d richtext tests passed!\n", tests_passed);
    return 0;
}
