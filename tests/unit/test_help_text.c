/* Unit tests for help text ownership and language selection */

#include "../../include/help_text.h"
#include "text_assert.h"
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
    char en[8192] = {0};
    char zh[8192] = {0};
    size_t en_pos = 0;
    size_t zh_pos = 0;

    help_text_append_full(en, sizeof(en), &en_pos, UI_LANG_EN);
    help_text_append_full(zh, sizeof(zh), &zh_pos, UI_LANG_ZH);

    assert(strstr(en, "TNT KEY REFERENCE") != NULL);
    assert(strstr(en, "AVAILABLE COMMANDS") != NULL);
    assert(strstr(en, "COMMAND OUTPUT KEYS") != NULL);
    assert(strstr(en, ":inbox") != NULL);
    assert(strstr(en, ":support") == NULL);
    assert(strstr(en, ":commands") == NULL);
    assert(strstr(en, "Switch English/Chinese") != NULL);

    assert(strstr(zh, "TNT 按键参考") != NULL);
    assert(strstr(zh, "可用命令") != NULL);
    assert(strstr(zh, "命令输出按键") != NULL);
    assert(strstr(zh, ":inbox") != NULL);
    assert(strstr(zh, "/me <action>") != NULL);
    assert(strstr(zh, "@username") != NULL);
    assert(strstr(zh, "<动作>") == NULL);
    assert(strstr(zh, "@用户名") == NULL);
    assert(strstr(zh, ":support") == NULL);
    assert(strstr(zh, ":commands") == NULL);
    assert(strstr(zh, "切换英文/中文") != NULL);
    assert_ascii_angle_placeholders(zh);
}

int main(void) {
    printf("Running help text unit tests...\n\n");

    RUN_TEST(full_help_matches_language);

    printf("\n✓ All %d tests passed!\n", tests_passed);
    return 0;
}
