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
        "{\"type\":\"message.create\",\"plain_text\":\"\\u001b[2J\"}",
        &response));
    assert(!tnt_module_parse_message_create(
        "{\"type\":\"message.create\",\"plain_text\":\"next \xC2\x9B"
        "31m\"}", &response));
}

TEST(parse_message_create_accepts_a_newline) {
    tnt_module_message_create_t response;

    assert(tnt_module_parse_message_create(
        "{\"type\":\"message.create\",\"plain_text\":\"line\\nnext\"}",
        &response));
    assert(strcmp(response.plain_text, "line\nnext") == 0);
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

TEST(parse_message_post_accepts_valid_sender) {
    tnt_module_message_post_t post;

    assert(tnt_module_parse_message_post(
        "{\"type\":\"message.post\",\"sender\":\"alice\","
        "\"plain_text\":\"hi\\nthere\"}", &post));
    assert(strcmp(post.sender, "alice") == 0);
    assert(strcmp(post.plain_text, "hi\nthere") == 0);

    assert(tnt_module_parse_message_post(
        "{\"type\":\"message.post\",\"sender\":\"abcdefghijklmnopqrst\","
        "\"plain_text\":\"twenty\"}", &post));
}

TEST(parse_message_post_rejects_invalid_sender) {
    static const char *const senders[] = {
        "", "system", "module:echo", "*", "-dash", ".dot", "a|b", "a;b",
        "abcdefghijklmnopqrstu", "bad\\u0001", "a:b", "module:", ":lead"
    };
    tnt_module_message_post_t post;

    for (size_t i = 0; i < sizeof(senders) / sizeof(senders[0]); i++) {
        char line[256];

        snprintf(line, sizeof(line),
                 "{\"type\":\"message.post\",\"sender\":\"%s\","
                 "\"plain_text\":\"hi\"}", senders[i]);
        assert(!tnt_module_parse_message_post(line, &post));
    }
    assert(!tnt_module_parse_message_post(
        "{\"type\":\"message.post\",\"plain_text\":\"hi\"}", &post));
}

TEST(parse_message_post_rejects_bad_text_or_type) {
    tnt_module_message_post_t post;

    assert(!tnt_module_parse_message_post(
        "{\"type\":\"message.post\",\"sender\":\"alice\","
        "\"plain_text\":\"\"}", &post));
    assert(!tnt_module_parse_message_post(
        "{\"type\":\"message.post\",\"sender\":\"alice\","
        "\"plain_text\":\"\\u001b[2J\"}", &post));
    assert(!tnt_module_parse_message_post(
        "{\"type\":\"message.create\",\"sender\":\"alice\","
        "\"plain_text\":\"hi\"}", &post));
}

TEST(appends_presence_jsonl) {
    char out[256] = "";
    size_t pos = 0;

    assert(tnt_module_append_presence(out, sizeof(out), &pos,
                                      TNT_MODULE_EVENT_PRESENCE_JOINED,
                                      "carol", 0) == 0);
    assert(strcmp(out,
                  "{\"type\":\"presence.joined\",\"nickname\":\"carol\","
                  "\"timestamp\":\"1970-01-01T00:00:00Z\"}\n") == 0);
}

TEST(appends_presence_snapshot_jsonl) {
    const char *names[] = {"alice", "bob"};
    char out[256] = "";
    size_t pos = 0;

    assert(tnt_module_append_presence_snapshot(out, sizeof(out), &pos,
                                               names, 2) == 0);
    assert(strcmp(out,
                  "{\"type\":\"presence.snapshot\","
                  "\"nicknames\":[\"alice\",\"bob\"]}\n") == 0);

    out[0] = '\0';
    pos = 0;
    assert(tnt_module_append_presence_snapshot(out, sizeof(out), &pos,
                                               NULL, 0) == 0);
    assert(strcmp(out, "{\"type\":\"presence.snapshot\",\"nicknames\":[]}\n") ==
           0);
}

TEST(presence_snapshot_reports_overflow) {
    const char *names[] = {"alice", "bob"};
    char out[24] = "";
    size_t pos = 0;

    assert(tnt_module_append_presence_snapshot(out, sizeof(out), &pos,
                                               names, 2) < 0);
}

int main(void) {
    printf("Running module protocol unit tests...\n\n");

    RUN_TEST(appends_handshake_jsonl);
    RUN_TEST(appends_message_created_jsonl_with_escaping);
    RUN_TEST(parse_message_create_accepts_valid_plain_text);
    RUN_TEST(parse_message_create_accepts_utf8_plain_text);
    RUN_TEST(parse_message_create_rejects_wrong_type);
    RUN_TEST(parse_message_create_rejects_empty_or_control_text);
    RUN_TEST(parse_message_create_accepts_a_newline);
    RUN_TEST(parse_message_create_rejects_invalid_utf8_text);
    RUN_TEST(parse_message_create_rejects_overlong_text);
    RUN_TEST(parse_message_post_accepts_valid_sender);
    RUN_TEST(parse_message_post_rejects_invalid_sender);
    RUN_TEST(parse_message_post_rejects_bad_text_or_type);
    RUN_TEST(appends_presence_jsonl);
    RUN_TEST(appends_presence_snapshot_jsonl);
    RUN_TEST(presence_snapshot_reports_overflow);

    printf("\nAll %d module protocol tests passed.\n", tests_passed);
    return 0;
}
