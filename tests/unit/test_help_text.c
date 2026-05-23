/* Unit tests for help text ownership and language selection */

#include "../../include/help_text.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TEST(name) static void test_##name()
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    test_##name(); \
    printf("✓\n"); \
    tests_passed++; \
} while(0)

static int tests_passed = 0;

TEST(full_help_matches_language) {
    const char *en = help_text_full(LANG_EN);
    const char *zh = help_text_full(LANG_ZH);

    assert(strstr(en, "TERMINAL CHAT ROOM - HELP") != NULL);
    assert(strstr(en, "AVAILABLE COMMANDS") != NULL);
    assert(strstr(en, "Switch English/Chinese") != NULL);

    assert(strstr(zh, "终端聊天室 - 帮助") != NULL);
    assert(strstr(zh, "可用命令") != NULL);
    assert(strstr(zh, "切换英文/中文") != NULL);
}

TEST(command_help_matches_language) {
    char out[2048];
    size_t pos;

    out[0] = '\0';
    pos = 0;
    help_text_append_commands(out, sizeof(out), &pos, LANG_EN);
    assert(strstr(out, "Available Commands") != NULL);
    assert(strstr(out, "Show online users") != NULL);
    assert(pos == strlen(out));

    out[0] = '\0';
    pos = 0;
    help_text_append_commands(out, sizeof(out), &pos, LANG_ZH);
    assert(strstr(out, "可用命令") != NULL);
    assert(strstr(out, "显示在线用户") != NULL);
    assert(pos == strlen(out));
}

int main(void) {
    printf("Running help text unit tests...\n\n");

    RUN_TEST(full_help_matches_language);
    RUN_TEST(command_help_matches_language);

    printf("\n✓ All %d tests passed!\n", tests_passed);
    return 0;
}
