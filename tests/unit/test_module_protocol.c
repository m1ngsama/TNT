#include "../../include/module_protocol.h"

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

TEST(appends_handshake_jsonl) {
    char out[256] = "";
    size_t pos = 0;

    assert(tnt_module_append_handshake(out, sizeof(out), &pos, "9.9.9") == 0);
    assert(strcmp(out,
                  "{\"type\":\"handshake\",\"protocol\":\"tnt.module.v1\","
                  "\"server\":{\"name\":\"tnt\",\"version\":\"9.9.9\"}}\n") ==
           0);
}

TEST(appends_message_created_jsonl_with_escaping) {
    char out[512] = "";
    size_t pos = 0;
    message_t msg = {
        .timestamp = 0,
    };

    snprintf(msg.username, sizeof(msg.username), "%s", "alice");
    snprintf(msg.content, sizeof(msg.content), "%s", "hello \"core\"");

    assert(tnt_module_append_message_created(out, sizeof(out), &pos,
                                             "local-1", &msg) == 0);
    assert(strstr(out, "\"type\":\"message.created\"") != NULL);
    assert(strstr(out, "\"id\":\"local-1\"") != NULL);
    assert(strstr(out, "\"timestamp\":\"1970-01-01T00:00:00Z\"") != NULL);
    assert(strstr(out, "\"sender\":\"alice\"") != NULL);
    assert(strstr(out, "\"kind\":\"text\"") != NULL);
    assert(strstr(out, "\"plain_text\":\"hello \\\"core\\\"\"") != NULL);
    assert(out[strlen(out) - 1] == '\n');
}

TEST(parse_message_create_accepts_valid_plain_text) {
    tnt_module_message_create_t response;

    assert(tnt_module_parse_message_create(
        "{\"type\":\"message.create\",\"plain_text\":\"echo: hello\"}",
        &response));
    assert(strcmp(response.plain_text, "echo: hello") == 0);
}

TEST(parse_message_create_accepts_utf8_plain_text) {
    tnt_module_message_create_t response;

    assert(tnt_module_parse_message_create(
        "{\"type\":\"message.create\",\"plain_text\":\"echo: \\u4e2d\"}",
        &response));
    assert(strcmp(response.plain_text, "echo: \xE4\xB8\xAD") == 0);
}

TEST(parse_message_create_rejects_wrong_type) {
    tnt_module_message_create_t response;

    assert(!tnt_module_parse_message_create(
        "{\"type\":\"event.ok\",\"plain_text\":\"hello\"}", &response));
}

TEST(parse_message_create_rejects_empty_or_control_text) {
    tnt_module_message_create_t response;

    assert(!tnt_module_parse_message_create(
        "{\"type\":\"message.create\",\"plain_text\":\"\"}", &response));
    assert(!tnt_module_parse_message_create(
        "{\"type\":\"message.create\",\"plain_text\":\"line\\nnext\"}",
        &response));
}

TEST(parse_message_create_rejects_invalid_utf8_text) {
    tnt_module_message_create_t response;
    char line[] = "{\"type\":\"message.create\",\"plain_text\":\"bad \xC3\x28\"}";

    assert(!tnt_module_parse_message_create(line, &response));
}

TEST(parse_message_create_rejects_overlong_text) {
    char line[MAX_MESSAGE_LEN + 128];
    tnt_module_message_create_t response;
    size_t pos = 0;

    buffer_appendf(line, sizeof(line), &pos,
                   "{\"type\":\"message.create\",\"plain_text\":\"");
    for (int i = 0; i < MAX_MESSAGE_LEN; i++) {
        buffer_append_bytes(line, sizeof(line), &pos, "a", 1);
    }
    buffer_appendf(line, sizeof(line), &pos, "\"}");

    assert(!tnt_module_parse_message_create(line, &response));
}

int main(void) {
    printf("Running module protocol unit tests...\n\n");

    RUN_TEST(appends_handshake_jsonl);
    RUN_TEST(appends_message_created_jsonl_with_escaping);
    RUN_TEST(parse_message_create_accepts_valid_plain_text);
    RUN_TEST(parse_message_create_accepts_utf8_plain_text);
    RUN_TEST(parse_message_create_rejects_wrong_type);
    RUN_TEST(parse_message_create_rejects_empty_or_control_text);
    RUN_TEST(parse_message_create_rejects_invalid_utf8_text);
    RUN_TEST(parse_message_create_rejects_overlong_text);

    printf("\nAll %d module protocol tests passed.\n", tests_passed);
    return 0;
}
