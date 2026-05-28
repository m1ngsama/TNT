/* Unit tests for tntctl local help and diagnostic text */

#include "../../include/tntctl_text.h"
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

TEST(usage_matches_language) {
    char en[2048] = {0};
    char zh[2048] = {0};
    size_t en_pos = 0;
    size_t zh_pos = 0;

    tntctl_text_append_usage(en, sizeof(en), &en_pos, UI_LANG_EN);
    tntctl_text_append_usage(zh, sizeof(zh), &zh_pos, UI_LANG_ZH);

    assert(strstr(en, "Usage: tntctl [options] host command [args...]") != NULL);
    assert(strstr(en, "--host-key-checking MODE") != NULL);
    assert(strstr(en,
                  "help, health, users, stats, tail, dump, post, exit") != NULL);
    assert(strstr(zh, "用法: tntctl [options] host command [args...]") != NULL);
    assert(strstr(zh, "OpenSSH 主机密钥模式") != NULL);
    assert(strstr(zh,
                  "help, health, users, stats, tail, dump, post, exit") != NULL);
}

TEST(errors_match_language) {
    assert(strcmp(tntctl_text(UI_LANG_EN, TNTCTL_TEXT_INVALID_PORT),
                  "invalid port") == 0);
    assert(strcmp(tntctl_text(UI_LANG_ZH, TNTCTL_TEXT_INVALID_PORT),
                  "端口无效") == 0);
    assert(strcmp(tntctl_text(UI_LANG_EN, TNTCTL_TEXT_UNKNOWN_OPTION_FORMAT),
                  "unknown option: %s") == 0);
    assert(strcmp(tntctl_text(UI_LANG_ZH, TNTCTL_TEXT_UNKNOWN_OPTION_FORMAT),
                  "未知选项: %s") == 0);
    assert(strcmp(tntctl_text((ui_lang_t)99, TNTCTL_TEXT_INVALID_PORT),
                  "invalid port") == 0);
    assert(strcmp(tntctl_text(UI_LANG_EN,
                              (tntctl_text_id_t)TNTCTL_TEXT_COUNT), "") == 0);
}

int main(void) {
    printf("Running tntctl text unit tests...\n\n");

    RUN_TEST(usage_matches_language);
    RUN_TEST(errors_match_language);

    printf("\n✓ All %d tests passed!\n", tests_passed);
    return 0;
}
