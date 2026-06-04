#include "../../include/json_text.h"

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

TEST(append_string_escapes_json_controls) {
    char out[128] = "";
    size_t pos = 0;

    tnt_json_append_string(out, sizeof(out), &pos, "a\"b\\c\n\t");
    assert(strcmp(out, "\"a\\\"b\\\\c\\n\\t\"") == 0);
}

TEST(get_string_field_extracts_top_level_value) {
    char value[64];

    assert(tnt_json_get_string_field(
        "{\"type\":\"message.create\",\"plain_text\":\"hello\"}",
        "plain_text", value, sizeof(value)));
    assert(strcmp(value, "hello") == 0);
}

TEST(get_string_field_skips_nested_values) {
    char value[64];

    assert(tnt_json_get_string_field(
        "{\"nested\":{\"plain_text\":\"wrong\"},\"plain_text\":\"right\"}",
        "plain_text", value, sizeof(value)));
    assert(strcmp(value, "right") == 0);
}

TEST(get_string_field_decodes_escapes) {
    char value[64];

    assert(tnt_json_get_string_field(
        "{\"plain_text\":\"line\\nquote:\\\" ok\"}",
        "plain_text", value, sizeof(value)));
    assert(strcmp(value, "line\nquote:\" ok") == 0);
}

TEST(get_string_field_decodes_unicode_escape) {
    char value[64];

    assert(tnt_json_get_string_field(
        "{\"plain_text\":\"hello \\u4e2d\"}",
        "plain_text", value, sizeof(value)));
    assert(strcmp(value, "hello \xE4\xB8\xAD") == 0);
}

TEST(get_string_field_rejects_missing_and_malformed_values) {
    char value[64];

    assert(!tnt_json_get_string_field("{\"x\":\"y\"}", "plain_text",
                                      value, sizeof(value)));
    assert(!tnt_json_get_string_field("{\"plain_text\":123}", "plain_text",
                                      value, sizeof(value)));
    assert(!tnt_json_get_string_field("{\"plain_text\":\"unterminated}",
                                      "plain_text", value, sizeof(value)));
    assert(!tnt_json_get_string_field(
        "{\"plain_text\":\"ok\",\"unterminated\":\"x}",
        "plain_text", value, sizeof(value)));
}

TEST(get_string_field_rejects_output_overflow) {
    char value[4];

    assert(!tnt_json_get_string_field("{\"plain_text\":\"abcd\"}",
                                      "plain_text", value, sizeof(value)));
}

int main(void) {
    printf("Running JSON text unit tests...\n\n");

    RUN_TEST(append_string_escapes_json_controls);
    RUN_TEST(get_string_field_extracts_top_level_value);
    RUN_TEST(get_string_field_skips_nested_values);
    RUN_TEST(get_string_field_decodes_escapes);
    RUN_TEST(get_string_field_decodes_unicode_escape);
    RUN_TEST(get_string_field_rejects_missing_and_malformed_values);
    RUN_TEST(get_string_field_rejects_output_overflow);

    printf("\nAll %d JSON text tests passed.\n", tests_passed);
    return 0;
}
