/* Unit tests for i18n language selection and text lookup */

#include "../../include/i18n.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST(name) static void test_##name()
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    test_##name(); \
    printf("✓\n"); \
    tests_passed++; \
} while(0)

static int tests_passed = 0;

TEST(parse_explicit_languages) {
    help_lang_t lang;

    assert(i18n_parse_lang("zh", LANG_EN) == LANG_ZH);
    assert(i18n_parse_lang("zh_CN.UTF-8", LANG_EN) == LANG_ZH);
    assert(i18n_parse_lang("en", LANG_ZH) == LANG_EN);
    assert(i18n_parse_lang("en_US.UTF-8", LANG_ZH) == LANG_EN);
    assert(i18n_parse_lang("C", LANG_ZH) == LANG_EN);
    assert(i18n_parse_lang("POSIX", LANG_ZH) == LANG_EN);

    assert(i18n_try_parse_lang("zh", &lang) == true);
    assert(lang == LANG_ZH);
    assert(i18n_try_parse_lang("en", &lang) == true);
    assert(lang == LANG_EN);
    assert(i18n_try_parse_lang("cn", &lang) == false);
    assert(i18n_try_parse_lang("english", &lang) == false);
    assert(i18n_try_parse_lang("chinese", &lang) == false);
    assert(i18n_try_parse_lang("中文", &lang) == false);
    assert(i18n_try_parse_lang("英文", &lang) == false);
    assert(i18n_try_parse_lang("fr", &lang) == false);
}

TEST(parse_unknown_uses_fallback) {
    assert(i18n_parse_lang(NULL, LANG_ZH) == LANG_ZH);
    assert(i18n_parse_lang("", LANG_EN) == LANG_EN);
    assert(i18n_parse_lang("fr_FR.UTF-8", LANG_ZH) == LANG_ZH);
}

TEST(parse_ignores_surrounding_whitespace) {
    help_lang_t lang;

    assert(i18n_try_parse_lang("  zh  ", &lang) == true);
    assert(lang == LANG_ZH);
    assert(i18n_parse_lang("\ten_US.UTF-8\n", LANG_ZH) == LANG_EN);
    assert(i18n_try_parse_lang(" english ", &lang) == false);
    assert(i18n_try_parse_lang("zh CN", &lang) == false);

    setenv("TNT_LANG", " zh ", 1);
    setenv("LC_ALL", "en_US.UTF-8", 1);
    assert(i18n_default_lang() == LANG_ZH);
}

TEST(default_prefers_tnt_lang) {
    setenv("TNT_LANG", "zh_CN.UTF-8", 1);
    setenv("LC_ALL", "en_US.UTF-8", 1);
    assert(i18n_default_lang() == LANG_ZH);

    setenv("TNT_LANG", "en", 1);
    setenv("LC_ALL", "zh_CN.UTF-8", 1);
    assert(i18n_default_lang() == LANG_EN);
}

TEST(default_uses_locale_when_no_tnt_lang) {
    unsetenv("TNT_LANG");
    setenv("LC_ALL", "zh_CN.UTF-8", 1);
    assert(i18n_default_lang() == LANG_ZH);

    setenv("LC_ALL", "C", 1);
    assert(i18n_default_lang() == LANG_EN);
}

TEST(text_lookup_matches_language) {
    assert(strstr(i18n_text(LANG_EN, I18N_USERNAME_PROMPT),
                  "display name") != NULL);
    assert(strstr(i18n_text(LANG_ZH, I18N_USERNAME_PROMPT),
                  "用户名") != NULL);
    assert(strstr(i18n_text(LANG_EN, I18N_WELCOME_SUBTITLE),
                  "anonymous chat") != NULL);
    assert(strstr(i18n_text(LANG_ZH, I18N_WELCOME_SUBTITLE),
                  "匿名聊天室") != NULL);
    assert(strstr(i18n_text(LANG_EN, I18N_HELP_STATUS_FORMAT),
                  "KEY REFERENCE") != NULL);
    assert(strstr(i18n_text(LANG_ZH, I18N_HELP_STATUS_FORMAT),
                  "按键参考") != NULL);
    assert(strstr(i18n_text(LANG_EN, I18N_COMMAND_OUTPUT_TITLE),
                  "COMMAND") != NULL);
    assert(strstr(i18n_text(LANG_ZH, I18N_COMMAND_OUTPUT_TITLE),
                  "命令输出") != NULL);
    assert(strstr(i18n_text(LANG_EN, I18N_MOTD_CONTINUE_HINT),
                  "Press any key") != NULL);
    assert(strstr(i18n_text(LANG_ZH, I18N_MOTD_CONTINUE_HINT),
                  "按任意键") != NULL);
    assert(strstr(i18n_text(LANG_EN, I18N_TITLE_ONLINE_FORMAT),
                  "online") != NULL);
    assert(strstr(i18n_text(LANG_ZH, I18N_TITLE_ONLINE_FORMAT),
                  "在线") != NULL);
    assert(strstr(i18n_text(LANG_EN, I18N_IDLE_TIMEOUT_FORMAT),
                  "idle timeout") != NULL);
    assert(strstr(i18n_text(LANG_ZH, I18N_IDLE_TIMEOUT_FORMAT),
                  "空闲超时") != NULL);
    assert(strstr(i18n_text(LANG_EN, I18N_MSG_USAGE),
                  "msg <username>") != NULL);
    assert(strstr(i18n_text(LANG_ZH, I18N_MSG_USAGE),
                  "用户名") != NULL);
    assert(strstr(i18n_text(LANG_EN, I18N_SEARCH_HEADER_FORMAT),
                  "Search") != NULL);
    assert(strstr(i18n_text(LANG_ZH, I18N_SEARCH_HEADER_FORMAT),
                  "搜索") != NULL);
    assert(strstr(i18n_text(LANG_EN, I18N_LANG_CURRENT_FORMAT),
                  "lang <en|zh>") != NULL);
    assert(strstr(i18n_text(LANG_ZH, I18N_LANG_CURRENT_FORMAT),
                  "lang <en|zh>") != NULL);
    assert(strstr(i18n_text(LANG_EN, I18N_UNKNOWN_COMMAND_FORMAT),
                  "Unknown command") != NULL);
    assert(strstr(i18n_text(LANG_ZH, I18N_UNKNOWN_COMMAND_FORMAT),
                  "未知命令") != NULL);
    assert(strstr(i18n_text(LANG_EN, I18N_EXEC_HELP),
                  "TNT exec interface") != NULL);
    assert(strstr(i18n_text(LANG_EN, I18N_EXEC_HELP),
                  "support") == NULL);
    assert(strstr(i18n_text(LANG_ZH, I18N_EXEC_HELP),
                  "TNT exec 接口") != NULL);
    assert(strstr(i18n_text(LANG_ZH, I18N_EXEC_HELP),
                  "support") == NULL);
    assert(strstr(i18n_text(LANG_EN, I18N_EXEC_POST_EMPTY),
                  "message cannot be empty") != NULL);
    assert(strstr(i18n_text(LANG_ZH, I18N_EXEC_POST_EMPTY),
                  "消息不能为空") != NULL);
    assert(strstr(i18n_text(LANG_EN, I18N_EXEC_UNKNOWN_COMMAND_FORMAT),
                  "Unknown command") != NULL);
    assert(strstr(i18n_text(LANG_ZH, I18N_EXEC_UNKNOWN_COMMAND_FORMAT),
                  "未知命令") != NULL);
    assert(strcmp(i18n_lang_code(LANG_EN), "en") == 0);
    assert(strcmp(i18n_lang_code(LANG_ZH), "zh") == 0);
}

int main(void) {
    printf("Running i18n unit tests...\n\n");

    RUN_TEST(parse_explicit_languages);
    RUN_TEST(parse_unknown_uses_fallback);
    RUN_TEST(parse_ignores_surrounding_whitespace);
    RUN_TEST(default_prefers_tnt_lang);
    RUN_TEST(default_uses_locale_when_no_tnt_lang);
    RUN_TEST(text_lookup_matches_language);

    printf("\n✓ All %d tests passed!\n", tests_passed);
    return 0;
}
