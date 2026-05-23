/* Unit tests for support text language selection */

#include "../../include/support_text.h"
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

TEST(interactive_support_matches_language) {
    const char *en = support_text_interactive(LANG_EN);
    const char *zh = support_text_interactive(LANG_ZH);

    assert(strstr(en, "Support") != NULL);
    assert(strstr(en, "First minute") != NULL);
    assert(strstr(en, ":mute-joins") != NULL);

    assert(strstr(zh, "支持") != NULL);
    assert(strstr(zh, "第一次进来") != NULL);
    assert(strstr(zh, ":mute-joins") != NULL);
}

TEST(exec_support_matches_language) {
    const char *en = support_text_exec(LANG_EN);
    const char *zh = support_text_exec(LANG_ZH);

    assert(strstr(en, "TNT support") != NULL);
    assert(strstr(en, "Non-interactive checks") != NULL);
    assert(strstr(en, "stats --json") != NULL);

    assert(strstr(zh, "TNT 支持") != NULL);
    assert(strstr(zh, "非交互检查") != NULL);
    assert(strstr(zh, "stats --json") != NULL);
}

int main(void) {
    printf("Running support text unit tests...\n\n");

    RUN_TEST(interactive_support_matches_language);
    RUN_TEST(exec_support_matches_language);

    printf("\n✓ All %d tests passed!\n", tests_passed);
    return 0;
}
