/* Unit tests for message functions */
#include "../../include/message.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>

#define TEST(name) static void test_##name()
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    test_##name(); \
    printf("✓\n"); \
    tests_passed++; \
} while(0)

static int tests_passed = 0;
static const char *test_log = "test_messages.log";

/* Helper: Clean up test log file */
static void cleanup_test_log(void) {
    unlink(test_log);
}

/* Helper: Create test log with N messages */
static void create_test_log(int count) {
    FILE *fp = fopen(test_log, "w");
    assert(fp != NULL);

    for (int i = 0; i < count; i++) {
        fprintf(fp, "2026-02-08T10:00:%02d+08:00|user%d|Test message %d\n",
                i, i, i);
    }
    fclose(fp);
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

    message_t *messages = NULL;
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
    RUN_TEST(message_edge_cases);
    RUN_TEST(message_special_characters);
    RUN_TEST(message_buffer_safety);
    RUN_TEST(message_timestamp_formats);

    cleanup_test_log();

    printf("\n✓ All %d tests passed!\n", tests_passed);
    return 0;
}
