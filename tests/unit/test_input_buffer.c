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
    size_t input_len = 0;

    assert(tnt_input_append_ascii(input, sizeof(input), &input_len, 'h') ==
           TNT_INPUT_APPEND_OK);
    assert(tnt_input_append_ascii(input, sizeof(input), &input_len, 'e') ==
           TNT_INPUT_APPEND_OK);
    assert(tnt_input_append_ascii(input, sizeof(input), &input_len, 'l') ==
           TNT_INPUT_APPEND_OK);
    assert(tnt_input_append_ascii(input, sizeof(input), &input_len, 'l') ==
           TNT_INPUT_APPEND_OK);
    assert(tnt_input_append_ascii(input, sizeof(input), &input_len, 'o') ==
           TNT_INPUT_APPEND_OK);
    assert(strcmp(input, "hello") == 0);
    assert(input_len == 5);
    assert(tnt_input_append_ascii(input, sizeof(input), &input_len, '!') ==
           TNT_INPUT_APPEND_OVERFLOW);
    assert(strcmp(input, "hello") == 0);
    assert(input_len == 5);
}

TEST(rejects_ascii_control_bytes) {
    char input[8] = "x";
    size_t input_len = 1;

    assert(tnt_input_append_ascii(input, sizeof(input), &input_len, '\n') ==
           TNT_INPUT_APPEND_IGNORED);
    assert(strcmp(input, "x") == 0);
    assert(input_len == 1);
}

TEST(appends_valid_utf8_sequence) {
    char input[16] = "hi ";
    size_t input_len = 3;

    assert(tnt_input_append_utf8_sequence(input, sizeof(input),
                                          &input_len, "\xE4\xB8\xAD", 3) ==
           TNT_INPUT_APPEND_OK);
    assert(strcmp(input, "hi \xE4\xB8\xAD") == 0);
    assert(input_len == 6);
}

TEST(rejects_invalid_utf8_sequence) {
    char input[16] = "";
    size_t input_len = 0;

    assert(tnt_input_append_utf8_sequence(input, sizeof(input),
                                          &input_len, "\xC3\x28", 2) ==
           TNT_INPUT_APPEND_INVALID_UTF8);
    assert(strcmp(input, "") == 0);
    assert(input_len == 0);
}

TEST(rejects_c1_control_sequence) {
    char input[16] = "safe";
    size_t input_len = 4;
    tnt_input_utf8_state_t state = {0};

    assert(tnt_input_append_utf8_sequence(input, sizeof(input), &input_len,
                                          "\xC2\x9B", 2) ==
           TNT_INPUT_APPEND_IGNORED);
    assert(tnt_input_append_stream_byte(input, sizeof(input), &input_len,
                                        &state, 0xC2, true) ==
           TNT_INPUT_APPEND_OK);
    assert(tnt_input_append_stream_byte(input, sizeof(input), &input_len,
                                        &state, 0x85, true) ==
           TNT_INPUT_APPEND_IGNORED);
    assert(strcmp(input, "safe") == 0);
    assert(input_len == 4);
}

TEST(paste_stream_normalizes_newlines_and_tabs) {
    char input[32] = "";
    size_t input_len = 0;
    tnt_input_utf8_state_t state = {0};

    assert(tnt_input_append_stream_byte(input, sizeof(input), &input_len,
                                        &state,
                                        'a', true) == TNT_INPUT_APPEND_OK);
    assert(tnt_input_append_stream_byte(input, sizeof(input), &input_len,
                                        &state,
                                        '\n', true) == TNT_INPUT_APPEND_OK);
    assert(tnt_input_append_stream_byte(input, sizeof(input), &input_len,
                                        &state,
                                        '\t', true) == TNT_INPUT_APPEND_OK);
    assert(tnt_input_append_stream_byte(input, sizeof(input), &input_len,
                                        &state,
                                        'b', true) == TNT_INPUT_APPEND_OK);
    assert(tnt_input_utf8_state_finish(&state) == TNT_INPUT_APPEND_OK);
    assert(strcmp(input, "a  b") == 0);
    assert(input_len == 4);
}

TEST(paste_stream_validates_multibyte_utf8) {
    char input[32] = "";
    size_t input_len = 0;
    tnt_input_utf8_state_t state = {0};

    assert(tnt_input_append_stream_byte(input, sizeof(input), &input_len,
                                        &state,
                                        0xE4, true) == TNT_INPUT_APPEND_OK);
    assert(tnt_input_append_stream_byte(input, sizeof(input), &input_len,
                                        &state,
                                        0xB8, true) == TNT_INPUT_APPEND_OK);
    assert(tnt_input_append_stream_byte(input, sizeof(input), &input_len,
                                        &state,
                                        0xAD, true) == TNT_INPUT_APPEND_OK);
    assert(tnt_input_utf8_state_finish(&state) == TNT_INPUT_APPEND_OK);
    assert(strcmp(input, "\xE4\xB8\xAD") == 0);
    assert(input_len == 3);
}

TEST(paste_stream_rejects_partial_utf8_at_end) {
    char input[32] = "";
    size_t input_len = 0;
    tnt_input_utf8_state_t state = {0};

    assert(tnt_input_append_stream_byte(input, sizeof(input), &input_len,
                                        &state,
                                        0xE4, true) == TNT_INPUT_APPEND_OK);
    assert(tnt_input_utf8_state_finish(&state) ==
           TNT_INPUT_APPEND_INVALID_UTF8);
    assert(strcmp(input, "") == 0);
    assert(input_len == 0);
}

TEST(paste_stream_drops_invalid_utf8_and_keeps_following_text) {
    char input[32] = "";
    size_t input_len = 0;
    tnt_input_utf8_state_t state = {0};
    int status;

    assert(tnt_input_append_stream_byte(input, sizeof(input), &input_len,
                                        &state,
                                        0xE4, true) == TNT_INPUT_APPEND_OK);
    status = tnt_input_append_stream_byte(input, sizeof(input), &input_len,
                                          &state, 'x', true);
    assert((status & TNT_INPUT_APPEND_INVALID_UTF8) != 0);
    assert(strcmp(input, "x") == 0);
    assert(input_len == 1);
}

int main(void) {
    printf("Running input buffer unit tests...\n\n");

    RUN_TEST(appends_ascii_until_capacity);
    RUN_TEST(rejects_ascii_control_bytes);
    RUN_TEST(appends_valid_utf8_sequence);
    RUN_TEST(rejects_invalid_utf8_sequence);
    RUN_TEST(rejects_c1_control_sequence);
    RUN_TEST(paste_stream_normalizes_newlines_and_tabs);
    RUN_TEST(paste_stream_validates_multibyte_utf8);
    RUN_TEST(paste_stream_rejects_partial_utf8_at_end);
    RUN_TEST(paste_stream_drops_invalid_utf8_and_keeps_following_text);

    printf("\nAll %d input buffer tests passed.\n", tests_passed);
    return 0;
}
