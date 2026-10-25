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

TEST(get_string_array_field_extracts_strings) {
    char items[3][8];
    size_t count = 99;

    assert(tnt_json_get_string_array_field(
        "{\"type\":\"x\",\"names\": [ \"a\" , \"b\\u00e9\" ]}", "names",
        &items[0][0], sizeof(items[0]), 3, &count));
    assert(count == 2);
    assert(strcmp(items[0], "a") == 0);
    assert(strcmp(items[1], "b\xc3\xa9") == 0);

    assert(tnt_json_get_string_array_field("{\"names\":[]}", "names",
                                           &items[0][0], sizeof(items[0]), 3,
                                           &count));
    assert(count == 0);
}

TEST(get_string_array_field_rejects_bad_arrays) {
    static const char *const lines[] = {
        "{\"names\":\"a\"}",
        "{\"names\":[1]}",
        "{\"names\":[\"a\",]}",
        "{\"names\":[\"a\" \"b\"]}",
        "{\"names\":[\"a\"",
        "{\"names\":[\"a\",\"b\",\"c\",\"d\"]}",
        "{\"names\":[\"abcdefgh\"]}",
        "{\"other\":[\"a\"]}",
    };
    char items[3][8];
    size_t count;

    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        assert(!tnt_json_get_string_array_field(lines[i], "names",
                                                &items[0][0],
                                                sizeof(items[0]), 3, &count));
        assert(count == 0);
    }
}

TEST(integer_fields_reject_non_integer_and_overflow_values) {
    int value = 0;
    assert(tnt_json_get_int_field("{\"delay\":3000}", "delay", &value) && value == 3000);
    assert(tnt_json_get_int_field("{\"delay\":-1}", "delay", &value) && value == -1);
    assert(!tnt_json_get_int_field("{\"delay\":1.5}", "delay", &value));
    assert(!tnt_json_get_int_field("{\"delay\":01}", "delay", &value));
    assert(!tnt_json_get_int_field("{\"delay\":\"3000\"}", "delay", &value));
    assert(!tnt_json_get_int_field("{\"delay\":2147483648}", "delay", &value));
    assert(!tnt_json_get_int_field("{\"delay\":1e2}", "delay", &value));
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
    RUN_TEST(get_string_array_field_extracts_strings);
    RUN_TEST(get_string_array_field_rejects_bad_arrays);
    RUN_TEST(integer_fields_reject_non_integer_and_overflow_values);

    printf("\nAll %d JSON text tests passed.\n", tests_passed);
    return 0;
}
