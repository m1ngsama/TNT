/* Unit tests for concise manual text language selection */

#include "../../include/manual_text.h"
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

TEST(interactive_manual_matches_language) {
    const char *en = manual_text_interactive(LANG_EN);
    const char *zh = manual_text_interactive(LANG_ZH);

    assert(strstr(en, "TNT(1) help") != NULL);
    assert(strstr(en, "Quick start") != NULL);
    assert(strstr(en, "Commands") != NULL);
    assert(strstr(en, ":mute-joins") != NULL);
    assert(strstr(en, ":support") == NULL);
    assert(strstr(en, ":commands") == NULL);

    assert(strstr(zh, "TNT(1) 帮助") != NULL);
    assert(strstr(zh, "快速开始") != NULL);
    assert(strstr(zh, "命令") != NULL);
    assert(strstr(zh, ":mute-joins") != NULL);
    assert(strstr(zh, ":support") == NULL);
    assert(strstr(zh, ":commands") == NULL);
}

int main(void) {
    printf("Running manual text unit tests...\n\n");

    RUN_TEST(interactive_manual_matches_language);

    printf("\n✓ All %d tests passed!\n", tests_passed);
    return 0;
}
