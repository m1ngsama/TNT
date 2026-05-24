/* Unit tests for command-line help and error text */

#include "../../include/cli_text.h"
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

TEST(help_matches_language) {
    char output[2048] = {0};
    size_t pos = 0;

    cli_text_append_help(output, sizeof(output), &pos, "tnt", UI_LANG_EN);
    assert(strstr(output, "anonymous SSH chat server") != NULL);
    assert(strstr(output, "Usage: tnt [options]") != NULL);
    assert(strstr(output, "TNT_LANG") != NULL);

    memset(output, 0, sizeof(output));
    pos = 0;
    cli_text_append_help(output, sizeof(output), &pos, "tnt", UI_LANG_ZH);
    assert(strstr(output, "匿名 SSH 聊天服务器") != NULL);
    assert(strstr(output, "用法: tnt [选项]") != NULL);
    assert(strstr(output, "TNT_LANG") != NULL);
}

TEST(error_formats_match_language) {
    assert(strcmp(cli_text_invalid_port_format(UI_LANG_EN),
                  "Invalid port: %s\n") == 0);
    assert(strcmp(cli_text_invalid_port_format(UI_LANG_ZH),
                  "端口无效: %s\n") == 0);
    assert(strcmp(cli_text_unknown_option_format(UI_LANG_EN),
                  "Unknown option: %s\n") == 0);
    assert(strcmp(cli_text_unknown_option_format(UI_LANG_ZH),
                  "未知选项: %s\n") == 0);
    assert(strcmp(cli_text_short_usage_format(UI_LANG_EN),
                  "Usage: %s [-p PORT] [-d DIR] [-h]\n") == 0);
    assert(strcmp(cli_text_short_usage_format(UI_LANG_ZH),
                  "用法: %s [-p PORT] [-d DIR] [-h]\n") == 0);
}

int main(void) {
    printf("Running CLI text unit tests...\n\n");

    RUN_TEST(help_matches_language);
    RUN_TEST(error_formats_match_language);

    printf("\n✓ All %d tests passed!\n", tests_passed);
    return 0;
}
