/* Unit tests for message functions */
#include "../../include/message.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>

#define TEST(name) static void test_##name()
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    test_##name(); \
    printf("✓\n"); \
    tests_passed++; \
} while(0)

static int tests_passed = 0;
static const char *test_log = "test_messages.log";
static char test_state_dir[PATH_MAX];

/* Helper: Clean up test log file */
static void cleanup_test_log(void) {
    unlink(test_log);
}

static void cleanup_state_dir(void) {
    if (test_state_dir[0] != '\0') {
        char log_path[PATH_MAX];
        snprintf(log_path, sizeof(log_path), "%s/messages.log", test_state_dir);
        unlink(log_path);
        rmdir(test_state_dir);
        test_state_dir[0] = '\0';
    }
    unsetenv("TNT_STATE_DIR");
}

static void setup_state_dir(void) {
    const char *tmp = getenv("TMPDIR");

    cleanup_state_dir();
    if (!tmp || tmp[0] == '\0') {
        tmp = "/tmp";
    }
    snprintf(test_state_dir, sizeof(test_state_dir),
             "%s/tnt-message-test.XXXXXX", tmp);
    assert(mkdtemp(test_state_dir) != NULL);
    assert(setenv("TNT_STATE_DIR", test_state_dir, 1) == 0);
}

static void format_rfc3339_now(char *buffer, size_t buf_size) {
    time_t now = time(NULL);
    struct tm tm_info;

    gmtime_r(&now, &tm_info);
    strftime(buffer, buf_size, "%Y-%m-%dT%H:%M:%SZ", &tm_info);
}

/* Test message initialization */
TEST(message_init) {
    message_init();
    /* No assertion needed, just ensure it doesn't crash */
}

/* Test loading from empty file */
TEST(message_load_empty) {
    cleanup_test_log();

    /* Temporarily override LOG_FILE */
    FILE *fp = fopen(test_log, "w");
    fclose(fp);

    /* Can't easily override LOG_FILE constant, so this is a documentation test */

    cleanup_test_log();
}

/* Test message format */
TEST(message_format_basic) {
    message_t msg;
    msg.timestamp = 1234567890;
    strcpy(msg.username, "testuser");
    strcpy(msg.content, "Hello World");

    char buffer[512];
    message_format(&msg, buffer, sizeof(buffer), 80);

    /* Should contain timestamp, username, and content */
    assert(strstr(buffer, "testuser") != NULL);
    assert(strstr(buffer, "Hello World") != NULL);
}

TEST(message_format_long_content) {
    message_t msg;
    msg.timestamp = 1234567890;
    strcpy(msg.username, "user");

    /* Create long message */
    memset(msg.content, 'A', MAX_MESSAGE_LEN - 1);
    msg.content[MAX_MESSAGE_LEN - 1] = '\0';

    char buffer[2048];
    message_format(&msg, buffer, sizeof(buffer), 80);

    /* Should not overflow */
    assert(strlen(buffer) < sizeof(buffer));
}

TEST(message_format_unicode) {
    message_t msg;
    msg.timestamp = 1234567890;
    strcpy(msg.username, "用户");
    strcpy(msg.content, "你好世界");

    char buffer[512];
    message_format(&msg, buffer, sizeof(buffer), 80);

    assert(strstr(buffer, "用户") != NULL);
    assert(strstr(buffer, "你好世界") != NULL);
}

TEST(message_format_width_limits) {
    message_t msg;
    msg.timestamp = 1234567890;
    strcpy(msg.username, "user");
    strcpy(msg.content, "Test");

    char buffer[512];

    /* Test various widths */
    message_format(&msg, buffer, sizeof(buffer), 40);
    assert(strlen(buffer) < 512);

    message_format(&msg, buffer, sizeof(buffer), 80);
    assert(strlen(buffer) < 512);

    message_format(&msg, buffer, sizeof(buffer), 120);
    assert(strlen(buffer) < 512);
}

/* Test message save */
TEST(message_save_basic) {
    cleanup_test_log();

    /* This is harder to test without modifying LOG_FILE constant */
    /* For now, document expected behavior */
    message_t msg;
    msg.timestamp = time(NULL);
    strcpy(msg.username, "testuser");
    strcpy(msg.content, "Test message");

    /* Would save to LOG_FILE */
    /* int ret = message_save(&msg); */
    /* assert(ret == 0); */

    cleanup_test_log();
}

TEST(message_load_skips_malformed_records) {
    char ts[64];
    char log_path[PATH_MAX];
    message_t *messages = NULL;

    setup_state_dir();
    format_rfc3339_now(ts, sizeof(ts));
    snprintf(log_path, sizeof(log_path), "%s/messages.log", test_state_dir);

    FILE *fp = fopen(log_path, "wb");
    assert(fp != NULL);
    fprintf(fp, "%s|alice|valid one\n", ts);
    fprintf(fp, "not-a-date|bob|bad date\n");
    fprintf(fp, "%s||empty user\n", ts);
    fprintf(fp, "%s|mallory|extra|pipe\n", ts);
    fprintf(fp, "%s|badutf|bad \xC3\x28\n", ts);
    fprintf(fp, "%s|partial|truncated record", ts);
    fclose(fp);

    int count = message_load(&messages, 10);
    assert(count == 1);
    assert(strcmp(messages[0].username, "alice") == 0);
    assert(strcmp(messages[0].content, "valid one") == 0);
    free(messages);
    cleanup_state_dir();
}

TEST(message_search_skips_malformed_records) {
    char ts[64];
    char log_path[PATH_MAX];
    message_t *results = NULL;

    setup_state_dir();
    format_rfc3339_now(ts, sizeof(ts));
    snprintf(log_path, sizeof(log_path), "%s/messages.log", test_state_dir);

    FILE *fp = fopen(log_path, "wb");
    assert(fp != NULL);
    fprintf(fp, "%s|alice|needle valid\n", ts);
    fprintf(fp, "%s|mallory|needle extra|pipe\n", ts);
    fprintf(fp, "%s|partial|needle truncated", ts);
    fclose(fp);

    int count = message_search("needle", &results, 10);
    assert(count == 1);
    assert(strcmp(results[0].username, "alice") == 0);
    assert(strcmp(results[0].content, "needle valid") == 0);
    free(results);
    cleanup_state_dir();
}

/* Test edge cases */
TEST(message_edge_cases) {
    message_t msg;
    char buffer[512];

    /* Empty username */
    msg.timestamp = 1234567890;
    msg.username[0] = '\0';
    strcpy(msg.content, "Test");
    message_format(&msg, buffer, sizeof(buffer), 80);
    assert(strlen(buffer) > 0);

    /* Empty content */
    strcpy(msg.username, "user");
    msg.content[0] = '\0';
    message_format(&msg, buffer, sizeof(buffer), 80);
    assert(strlen(buffer) > 0);

    /* Maximum length username */
    memset(msg.username, 'A', MAX_USERNAME_LEN - 1);
    msg.username[MAX_USERNAME_LEN - 1] = '\0';
    strcpy(msg.content, "Test");
    message_format(&msg, buffer, sizeof(buffer), 80);
    assert(strlen(buffer) < sizeof(buffer));

    /* Maximum length content */
    strcpy(msg.username, "user");
    memset(msg.content, 'B', MAX_MESSAGE_LEN - 1);
    msg.content[MAX_MESSAGE_LEN - 1] = '\0';
    message_format(&msg, buffer, sizeof(buffer), 80);
    /* Should handle gracefully */
}

TEST(message_special_characters) {
    message_t msg;
    char buffer[512];

    msg.timestamp = 1234567890;
    strcpy(msg.username, "user<test>");
    strcpy(msg.content, "Message with\nnewline\tand\ttabs");

    message_format(&msg, buffer, sizeof(buffer), 80);

    /* Should not crash or overflow */
    assert(strlen(buffer) < sizeof(buffer));
}

/* Test buffer safety */
TEST(message_buffer_safety) {
    message_t msg;
    char small_buffer[16];

    msg.timestamp = 1234567890;
    strcpy(msg.username, "verylongusername");
    strcpy(msg.content, "Very long message content that exceeds buffer");

    /* Should not overflow even with small buffer */
    message_format(&msg, small_buffer, sizeof(small_buffer), 80);
    assert(strlen(small_buffer) < sizeof(small_buffer));
}

/* Test timestamp handling */
TEST(message_timestamp_formats) {
    message_t msg;
    char buffer[512];

    strcpy(msg.username, "user");
    strcpy(msg.content, "Test");

    /* Test various timestamps */
    msg.timestamp = 0;  /* Epoch */
    message_format(&msg, buffer, sizeof(buffer), 80);
    assert(strlen(buffer) > 0);

    msg.timestamp = time(NULL);  /* Current time */
    message_format(&msg, buffer, sizeof(buffer), 80);
    assert(strlen(buffer) > 0);

    msg.timestamp = 2147483647;  /* Max 32-bit timestamp */
    message_format(&msg, buffer, sizeof(buffer), 80);
    assert(strlen(buffer) > 0);
}

int main(void) {
    printf("Running message unit tests...\n\n");

    RUN_TEST(message_init);
    RUN_TEST(message_load_empty);
    RUN_TEST(message_format_basic);
    RUN_TEST(message_format_long_content);
    RUN_TEST(message_format_unicode);
    RUN_TEST(message_format_width_limits);
    RUN_TEST(message_save_basic);
    RUN_TEST(message_load_skips_malformed_records);
    RUN_TEST(message_search_skips_malformed_records);
    RUN_TEST(message_edge_cases);
    RUN_TEST(message_special_characters);
    RUN_TEST(message_buffer_safety);
    RUN_TEST(message_timestamp_formats);

    cleanup_test_log();
    cleanup_state_dir();

    printf("\n✓ All %d tests passed!\n", tests_passed);
    return 0;
}
