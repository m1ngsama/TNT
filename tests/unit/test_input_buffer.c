#include "../../include/input_buffer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    test_##name(); \
    printf("ok\n"); \
    tests_passed++; \
} while (0)

static int tests_passed = 0;

TEST(appends_ascii_until_capacity) {
    char input[6] = "";

    assert(tnt_input_append_ascii(input, sizeof(input), 'h') ==
           TNT_INPUT_APPEND_OK);
    assert(tnt_input_append_ascii(input, sizeof(input), 'e') ==
           TNT_INPUT_APPEND_OK);
    assert(tnt_input_append_ascii(input, sizeof(input), 'l') ==
           TNT_INPUT_APPEND_OK);
    assert(tnt_input_append_ascii(input, sizeof(input), 'l') ==
           TNT_INPUT_APPEND_OK);
    assert(tnt_input_append_ascii(input, sizeof(input), 'o') ==
           TNT_INPUT_APPEND_OK);
    assert(strcmp(input, "hello") == 0);
    assert(tnt_input_append_ascii(input, sizeof(input), '!') ==
           TNT_INPUT_APPEND_OVERFLOW);
    assert(strcmp(input, "hello") == 0);
}

TEST(rejects_ascii_control_bytes) {
    char input[8] = "x";

    assert(tnt_input_append_ascii(input, sizeof(input), '\n') ==
           TNT_INPUT_APPEND_IGNORED);
    assert(strcmp(input, "x") == 0);
}

TEST(appends_valid_utf8_sequence) {
    char input[16] = "hi ";

    assert(tnt_input_append_utf8_sequence(input, sizeof(input),
                                          "\xE4\xB8\xAD", 3) ==
           TNT_INPUT_APPEND_OK);
    assert(strcmp(input, "hi \xE4\xB8\xAD") == 0);
}

TEST(rejects_invalid_utf8_sequence) {
    char input[16] = "";

    assert(tnt_input_append_utf8_sequence(input, sizeof(input),
                                          "\xC3\x28", 2) ==
           TNT_INPUT_APPEND_INVALID_UTF8);
    assert(strcmp(input, "") == 0);
}

TEST(paste_stream_normalizes_newlines_and_tabs) {
    char input[32] = "";
    tnt_input_utf8_state_t state = {0};

    assert(tnt_input_append_stream_byte(input, sizeof(input), &state,
                                        'a', true) == TNT_INPUT_APPEND_OK);
    assert(tnt_input_append_stream_byte(input, sizeof(input), &state,
                                        '\n', true) == TNT_INPUT_APPEND_OK);
    assert(tnt_input_append_stream_byte(input, sizeof(input), &state,
                                        '\t', true) == TNT_INPUT_APPEND_OK);
    assert(tnt_input_append_stream_byte(input, sizeof(input), &state,
                                        'b', true) == TNT_INPUT_APPEND_OK);
    assert(tnt_input_utf8_state_finish(&state) == TNT_INPUT_APPEND_OK);
    assert(strcmp(input, "a  b") == 0);
}

TEST(paste_stream_validates_multibyte_utf8) {
    char input[32] = "";
    tnt_input_utf8_state_t state = {0};

    assert(tnt_input_append_stream_byte(input, sizeof(input), &state,
                                        0xE4, true) == TNT_INPUT_APPEND_OK);
    assert(tnt_input_append_stream_byte(input, sizeof(input), &state,
                                        0xB8, true) == TNT_INPUT_APPEND_OK);
    assert(tnt_input_append_stream_byte(input, sizeof(input), &state,
                                        0xAD, true) == TNT_INPUT_APPEND_OK);
    assert(tnt_input_utf8_state_finish(&state) == TNT_INPUT_APPEND_OK);
    assert(strcmp(input, "\xE4\xB8\xAD") == 0);
}

TEST(paste_stream_rejects_partial_utf8_at_end) {
    char input[32] = "";
    tnt_input_utf8_state_t state = {0};

    assert(tnt_input_append_stream_byte(input, sizeof(input), &state,
                                        0xE4, true) == TNT_INPUT_APPEND_OK);
    assert(tnt_input_utf8_state_finish(&state) ==
           TNT_INPUT_APPEND_INVALID_UTF8);
    assert(strcmp(input, "") == 0);
}

TEST(paste_stream_drops_invalid_utf8_and_keeps_following_text) {
    char input[32] = "";
    tnt_input_utf8_state_t state = {0};
    int status;

    assert(tnt_input_append_stream_byte(input, sizeof(input), &state,
                                        0xE4, true) == TNT_INPUT_APPEND_OK);
    status = tnt_input_append_stream_byte(input, sizeof(input), &state,
                                          'x', true);
    assert((status & TNT_INPUT_APPEND_INVALID_UTF8) != 0);
    assert(strcmp(input, "x") == 0);
}

int main(void) {
    printf("Running input buffer unit tests...\n\n");

    RUN_TEST(appends_ascii_until_capacity);
    RUN_TEST(rejects_ascii_control_bytes);
    RUN_TEST(appends_valid_utf8_sequence);
    RUN_TEST(rejects_invalid_utf8_sequence);
    RUN_TEST(paste_stream_normalizes_newlines_and_tabs);
    RUN_TEST(paste_stream_validates_multibyte_utf8);
    RUN_TEST(paste_stream_rejects_partial_utf8_at_end);
    RUN_TEST(paste_stream_drops_invalid_utf8_and_keeps_following_text);

    printf("\nAll %d input buffer tests passed.\n", tests_passed);
    return 0;
}
