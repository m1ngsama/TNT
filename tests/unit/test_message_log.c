/* Unit tests for the message log record encoding */
#include "../../include/message_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>

#define TEST(name) static void test_##name()
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    test_##name(); \
    printf("✓\n"); \
    tests_passed++; \
} while(0)

static int tests_passed = 0;

static void write_file(const char *path, const char *text) {
    FILE *fp = fopen(path, "w");

    assert(fp != NULL);
    assert(fputs(text, fp) >= 0);
    assert(fclose(fp) == 0);
}

static void read_file(const char *path, char *out, size_t out_size) {
    FILE *fp = fopen(path, "r");
    size_t n;

    assert(fp != NULL);
    n = fread(out, 1, out_size - 1, fp);
    out[n] = '\0';
    fclose(fp);
}

static bool file_exists(const char *path) {
    return access(path, F_OK) == 0;
}

TEST(encode_escapes_backslash_and_newline) {
    char out[64];

    assert(message_log_encode_content("a\\b", out, sizeof(out)));
    assert(strcmp(out, "a\\\\b") == 0);

    assert(message_log_encode_content("one\ntwo", out, sizeof(out)));
    assert(strcmp(out, "one\\ntwo") == 0);

    assert(message_log_encode_content("plain", out, sizeof(out)));
    assert(strcmp(out, "plain") == 0);
}

TEST(decode_is_the_inverse_of_encode) {
    const char *cases[] = {"plain", "a\\b", "one\ntwo", "\\n", "\\\\",
                           "中文\nemoji😌", "\n", "trailing\\", NULL};

    for (int i = 0; cases[i]; i++) {
        char enc[512];
        char dec[512];

        assert(message_log_encode_content(cases[i], enc, sizeof(enc)));
        assert(message_log_decode_content(enc, dec, sizeof(dec)));
        assert(strcmp(dec, cases[i]) == 0);
    }
}

TEST(encode_rejects_overflow) {
    char out[8];

    /* Eight backslashes need sixteen bytes plus a terminator. */
    assert(!message_log_encode_content("\\\\\\\\\\\\\\\\", out, sizeof(out)));
}

TEST(decode_rejects_a_malformed_escape) {
    char out[64];

    /* A trailing lone backslash is not something the encoder can produce. */
    assert(!message_log_decode_content("bad\\", out, sizeof(out)));
    /* Nor is an unknown escape letter. */
    assert(!message_log_decode_content("bad\\x", out, sizeof(out)));
}

TEST(record_round_trip_carries_a_newline) {
    message_t msg = {0};
    message_t back = {0};
    char line[MESSAGE_LOG_MAX_LINE];

    msg.timestamp = 1704067200;
    snprintf(msg.username, sizeof(msg.username), "alice");
    snprintf(msg.content, sizeof(msg.content), "first\nsecond");

    assert(message_log_format_record(&msg, line, sizeof(line), NULL) == 0);
    assert(strstr(line, "first\\nsecond") != NULL);          /* stored escaped */
    assert(strchr(line, '\n') == line + strlen(line) - 1);   /* still one line */

    assert(message_log_parse_record(line, &back, msg.timestamp));
    assert(strcmp(back.content, "first\nsecond") == 0);      /* decoded back */
}

TEST(record_still_rejects_other_control_characters) {
    message_t msg = {0};
    char line[MESSAGE_LOG_MAX_LINE];

    msg.timestamp = 1704067200;
    snprintf(msg.username, sizeof(msg.username), "alice");

    snprintf(msg.content, sizeof(msg.content), "esc\033[2Jhere");
    assert(message_log_format_record(&msg, line, sizeof(line), NULL) < 0);

    snprintf(msg.content, sizeof(msg.content), "del\177here");
    assert(message_log_format_record(&msg, line, sizeof(line), NULL) < 0);

    snprintf(msg.content, sizeof(msg.content), "tab\there");
    assert(message_log_format_record(&msg, line, sizeof(line), NULL) < 0);
}

TEST(username_still_rejects_newline) {
    message_t msg = {0};
    char line[MESSAGE_LOG_MAX_LINE];

    msg.timestamp = 1704067200;
    snprintf(msg.username, sizeof(msg.username), "a\nb");
    snprintf(msg.content, sizeof(msg.content), "hi");
    assert(message_log_format_record(&msg, line, sizeof(line), NULL) < 0);
}

TEST(header_is_recognised_and_is_not_a_record) {
    message_t out = {0};

    assert(message_log_is_header(MESSAGE_LOG_HEADER "\n"));
    assert(!message_log_is_header("2024-01-01T00:00:00Z|a|b\n"));
    assert(!message_log_parse_record(MESSAGE_LOG_HEADER "\n", &out,
                                     time(NULL)));
}

TEST(a_v1_record_with_a_backslash_survives_migration) {
    /* Migration escapes the backslash, so the decoded text still matches the
     * original.  This is the property that makes migration lossless. */
    char enc[128];
    char dec[128];

    assert(message_log_encode_content("C:\\new", enc, sizeof(enc)));
    assert(strcmp(enc, "C:\\\\new") == 0);
    assert(message_log_decode_content(enc, dec, sizeof(dec)));
    assert(strcmp(dec, "C:\\new") == 0);
}

TEST(encoded_length_counts_the_escapes) {
    assert(message_log_encoded_length("plain") == 5);
    assert(message_log_encoded_length("a\\b") == 4);
    assert(message_log_encoded_length("one\ntwo") == 8);
    assert(message_log_encoded_length("") == 0);
    assert(message_log_encoded_length(NULL) == 0);
}

TEST(migration_escapes_backslashes_and_is_idempotent) {
    char dir[] = "/tmp/tnt-migrate-XXXXXX";
    char path[512];
    char backup[512];
    char first[512];
    char second[512];

    assert(mkdtemp(dir) != NULL);
    snprintf(path, sizeof(path), "%s/messages.log", dir);
    snprintf(backup, sizeof(backup), "%s/messages.log.v1.bak", dir);

    write_file(path, "2024-01-01T00:00:00Z|alice|C:\\new\n"
                     "2024-01-01T00:00:01Z|bob|plain\n");

    assert(message_log_migrate(path) == 0);
    read_file(path, first, sizeof(first));
    assert(strncmp(first, MESSAGE_LOG_HEADER, strlen(MESSAGE_LOG_HEADER)) == 0);
    assert(strstr(first, "|alice|C:\\\\new\n") != NULL);
    assert(strstr(first, "|bob|plain\n") != NULL);
    assert(file_exists(backup));

    /* A second run must not escape the escape. */
    assert(message_log_migrate(path) == 0);
    read_file(path, second, sizeof(second));
    assert(strcmp(first, second) == 0);

    unlink(path);
    unlink(backup);
    rmdir(dir);
}

TEST(migration_preserves_what_a_record_means) {
    /* The decoded content after migration equals the raw content before it. */
    message_t out = {0};
    char line[MESSAGE_LOG_MAX_LINE];

    snprintf(line, sizeof(line), "2024-01-01T00:00:00Z|alice|C:\\\\new\n");
    assert(message_log_parse_record(line, &out, 1704067200));
    assert(strcmp(out.content, "C:\\new") == 0);
}

TEST(migration_of_a_missing_file_is_not_a_failure) {
    assert(message_log_migrate("/tmp/tnt-migrate-does-not-exist/messages.log")
           == 0);
}

TEST(migration_keeps_a_line_it_cannot_represent_out_of_the_log) {
    /* Escaping can push content past the field limit.  Such a record stays in
     * the backup rather than being written back in a form that decodes to
     * something else. */
    char dir[] = "/tmp/tnt-migrate-XXXXXX";
    char path[512];
    char backup[512];
    char seed[MESSAGE_LOG_MAX_LINE * 2];
    char migrated[MESSAGE_LOG_MAX_LINE * 2];
    char slashes[MAX_MESSAGE_LEN];
    size_t i;

    assert(mkdtemp(dir) != NULL);
    snprintf(path, sizeof(path), "%s/messages.log", dir);
    snprintf(backup, sizeof(backup), "%s/messages.log.v1.bak", dir);

    for (i = 0; i < sizeof(slashes) - 1; i++) {
        slashes[i] = '\\';
    }
    slashes[sizeof(slashes) - 1] = '\0';
    snprintf(seed, sizeof(seed),
             "2024-01-01T00:00:00Z|alice|%s\n"
             "2024-01-01T00:00:01Z|bob|kept\n", slashes);
    write_file(path, seed);

    assert(message_log_migrate(path) == 0);
    read_file(path, migrated, sizeof(migrated));
    assert(strstr(migrated, "|alice|") == NULL);
    assert(strstr(migrated, "|bob|kept\n") != NULL);
    assert(file_exists(backup));

    unlink(path);
    unlink(backup);
    rmdir(dir);
}

int main(void) {
    printf("Running message log unit tests...\n\n");
    RUN_TEST(encode_escapes_backslash_and_newline);
    RUN_TEST(decode_is_the_inverse_of_encode);
    RUN_TEST(encode_rejects_overflow);
    RUN_TEST(decode_rejects_a_malformed_escape);
    RUN_TEST(record_round_trip_carries_a_newline);
    RUN_TEST(record_still_rejects_other_control_characters);
    RUN_TEST(username_still_rejects_newline);
    RUN_TEST(header_is_recognised_and_is_not_a_record);
    RUN_TEST(a_v1_record_with_a_backslash_survives_migration);
    RUN_TEST(encoded_length_counts_the_escapes);
    RUN_TEST(migration_escapes_backslashes_and_is_idempotent);
    RUN_TEST(migration_preserves_what_a_record_means);
    RUN_TEST(migration_of_a_missing_file_is_not_a_failure);
    RUN_TEST(migration_keeps_a_line_it_cannot_represent_out_of_the_log);
    printf("\nAll %d message log tests passed!\n", tests_passed);
    return 0;
}
