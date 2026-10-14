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
                                             "local-1", &msg, false) == 0);
    assert(strstr(out, "\"type\":\"message.created\"") != NULL);
    assert(strstr(out, "\"id\":\"local-1\"") != NULL);
    assert(strstr(out, "\"timestamp\":\"1970-01-01T00:00:00Z\"") != NULL);
    assert(strstr(out, "\"sender\":\"alice\"") != NULL);
    assert(strstr(out, "\"kind\":\"text\"") != NULL);
    assert(strstr(out, "\"plain_text\":\"hello \\\"core\\\"\"") != NULL);
    assert(strstr(out, "\"metadata\":{}}}\n") != NULL);
    assert(out[strlen(out) - 1] == '\n');
}

TEST(appends_verified_metadata) {
    char out[512] = "";
    size_t pos = 0;
    message_t msg = {
        .timestamp = 0,
    };

    snprintf(msg.username, sizeof(msg.username), "%s", "alice");
    snprintf(msg.content, sizeof(msg.content), "%s", "hi");
    assert(tnt_module_append_message_created(out, sizeof(out), &pos,
                                             "local-2", &msg, true) == 0);
    assert(strstr(out, "\"metadata\":{\"verified\":true}}}\n") != NULL);
}

TEST(appends_identity_requested_jsonl) {
    char out[256] = "";
    size_t pos = 0;

    assert(tnt_module_append_identity_requested(out, sizeof(out), &pos,
                                                "s-7", "al\"ice") == 0);
    assert(strcmp(out, "{\"type\":\"identity.requested\",\"session\":\"s-7\","
                       "\"nickname\":\"al\\\"ice\"}\n") == 0);
}

TEST(parse_identity_records) {
    tnt_module_identity_record_t record;

    assert(tnt_module_parse_identity_record(
        "{\"type\":\"identity.notice\",\"session\":\"s-1\","
        "\"plain_text\":\"Visit https://auth.m1ng.space\\nCode: ABCD\"}",
        &record));
    assert(record.kind == TNT_MODULE_IDENTITY_NOTICE);
    assert(strcmp(record.session, "s-1") == 0);
    assert(strcmp(record.plain_text,
                  "Visit https://auth.m1ng.space\nCode: ABCD") == 0);

    assert(tnt_module_parse_identity_record(
        "{\"type\":\"identity.verified\",\"session\":\"s-1\","
        "\"handle\":\"alice_2\"}", &record));
    assert(record.kind == TNT_MODULE_IDENTITY_VERIFIED);
    assert(strcmp(record.handle, "alice_2") == 0);

    assert(tnt_module_parse_identity_record(
        "{\"type\":\"identity.reserve\",\"handle\":\"bob\"}", &record));
    assert(record.kind == TNT_MODULE_IDENTITY_RESERVE);
    assert(strcmp(record.handle, "bob") == 0);
}

TEST(parse_identity_records_rejects_invalid) {
    tnt_module_identity_record_t record;

    assert(!tnt_module_parse_identity_record(
        "{\"type\":\"identity.verified\",\"session\":\"s-1\","
        "\"handle\":\"Alice\"}", &record));
    assert(!tnt_module_parse_identity_record(
        "{\"type\":\"identity.verified\",\"handle\":\"alice\"}", &record));
    assert(!tnt_module_parse_identity_record(
        "{\"type\":\"identity.reserve\","
        "\"handle\":\"abcdefghijklmnopqrstu\"}", &record));
    assert(!tnt_module_parse_identity_record(
        "{\"type\":\"identity.notice\",\"session\":\"s-1\","
        "\"plain_text\":\"\"}", &record));
    assert(!tnt_module_parse_identity_record(
        "{\"type\":\"identity.notice\",\"session\":\"s-1\","
        "\"plain_text\":\"\\u001b[2J\"}", &record));
    assert(!tnt_module_parse_identity_record(
        "{\"type\":\"identity.forget\",\"handle\":\"alice\"}", &record));
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

TEST(appends_private_created_jsonl) {
    char out[512] = "";
    size_t pos = 0;

    assert(tnt_module_append_private_created(out, sizeof(out), &pos, "bob",
                                             "alice", "hi \"there\"\nok", 0,
                                             true) == 0);
    assert(strcmp(out,
                  "{\"type\":\"private.created\",\"from\":\"bob\","
                  "\"to\":\"alice\",\"plain_text\":\"hi \\\"there\\\"\\nok\","
                  "\"timestamp\":\"1970-01-01T00:00:00Z\","
                  "\"verified\":true}\n") == 0);

    out[0] = '\0';
    pos = 0;
    assert(tnt_module_append_private_created(out, sizeof(out), &pos, "bob",
                                             "alice", "x", 0, false) == 0);
    assert(strstr(out, ",\"verified\":false}\n") != NULL);

    pos = 0;
    assert(tnt_module_append_private_created(out, 16, &pos, "bob", "alice",
                                             "x", 0, false) < 0);
}

TEST(parse_presence_remote_accepts_names) {
    static tnt_module_presence_remote_t remote;

    assert(tnt_module_parse_presence_remote(
        "{\"type\":\"presence.remote\",\"nicknames\":[\"alice\",\"Bob\","
        "\"bob\",\"\u5c0f\u660e\"]}", &remote));
    assert(remote.count == 3);
    assert(strcmp(remote.nicknames[0], "alice") == 0);
    assert(strcmp(remote.nicknames[1], "Bob") == 0);
    assert(strcmp(remote.nicknames[2], "\xe5\xb0\x8f\xe6\x98\x8e") == 0);

    assert(tnt_module_parse_presence_remote(
        "{\"type\":\"presence.remote\",\"nicknames\":[]}", &remote));
    assert(remote.count == 0);
}

TEST(parse_presence_remote_takes_256_names) {
    static tnt_module_presence_remote_t remote;
    char line[4096];
    size_t pos = 0;

    buffer_appendf(line, sizeof(line), &pos,
                   "{\"type\":\"presence.remote\",\"nicknames\":[");
    for (int i = 0; i < TNT_MODULE_REMOTE_USERS_MAX; i++) {
        buffer_appendf(line, sizeof(line), &pos, "%s\"%x\"", i ? "," : "", i);
    }
    buffer_appendf(line, sizeof(line), &pos, "]}");
    assert(tnt_module_parse_presence_remote(line, &remote));
    assert(remote.count == TNT_MODULE_REMOTE_USERS_MAX);

    pos -= 2;
    buffer_appendf(line, sizeof(line), &pos, ",\"zz\"]}");
    assert(!tnt_module_parse_presence_remote(line, &remote));
    assert(remote.count == 0);
}

TEST(parse_presence_remote_rejects_invalid) {
    static const char *const lines[] = {
        "{\"type\":\"presence.remote\"}",
        "{\"type\":\"presence.remote\",\"nicknames\":\"alice\"}",
        "{\"type\":\"presence.remote\",\"nicknames\":[\"alice\",1]}",
        "{\"type\":\"presence.remote\",\"nicknames\":[\"\"]}",
        "{\"type\":\"presence.remote\",\"nicknames\":[\"system\"]}",
        "{\"type\":\"presence.remote\",\"nicknames\":[\"a:b\"]}",
        "{\"type\":\"presence.remote\",\"nicknames\":[\"a|b\"]}",
        "{\"type\":\"presence.remote\","
        "\"nicknames\":[\"abcdefghijklmnopqrstu\"]}",
        "{\"type\":\"presence.snapshot\",\"nicknames\":[\"alice\"]}",
    };
    static tnt_module_presence_remote_t remote;

    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        assert(!tnt_module_parse_presence_remote(lines[i], &remote));
    }
}

TEST(parse_private_post) {
    tnt_module_private_post_t post;

    assert(tnt_module_parse_private_post(
        "{\"type\":\"private.post\",\"from\":\"alice\",\"to\":\"Bob\","
        "\"plain_text\":\"hi\\nthere\"}", &post));
    assert(strcmp(post.from, "alice") == 0);
    assert(strcmp(post.to, "Bob") == 0);
    assert(strcmp(post.plain_text, "hi\nthere") == 0);

    assert(!tnt_module_parse_private_post(
        "{\"type\":\"private.post\",\"from\":\"system\",\"to\":\"bob\","
        "\"plain_text\":\"hi\"}", &post));
    assert(!tnt_module_parse_private_post(
        "{\"type\":\"private.post\",\"from\":\"alice\","
        "\"plain_text\":\"hi\"}", &post));
    assert(!tnt_module_parse_private_post(
        "{\"type\":\"private.post\",\"from\":\"alice\",\"to\":\"bob\","
        "\"plain_text\":\"\"}", &post));
    assert(!tnt_module_parse_private_post(
        "{\"type\":\"private.post\",\"from\":\"alice\",\"to\":\"bob\","
        "\"plain_text\":\"\\u001b[2J\"}", &post));
    assert(!tnt_module_parse_private_post(
        "{\"type\":\"message.post\",\"from\":\"alice\",\"to\":\"bob\","
        "\"plain_text\":\"hi\"}", &post));
}

int main(void) {
    printf("Running module protocol unit tests...\n\n");

    RUN_TEST(appends_handshake_jsonl);
    RUN_TEST(appends_message_created_jsonl_with_escaping);
    RUN_TEST(appends_verified_metadata);
    RUN_TEST(appends_identity_requested_jsonl);
    RUN_TEST(parse_identity_records);
    RUN_TEST(parse_identity_records_rejects_invalid);
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
    RUN_TEST(appends_private_created_jsonl);
    RUN_TEST(parse_presence_remote_accepts_names);
    RUN_TEST(parse_presence_remote_takes_256_names);
    RUN_TEST(parse_presence_remote_rejects_invalid);
    RUN_TEST(parse_private_post);

    printf("\nAll %d module protocol tests passed.\n", tests_passed);
    return 0;
}
