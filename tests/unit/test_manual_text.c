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

static int count_lines(const char *text) {
    int lines = 0;

    while (text && *text) {
        if (*text == '\n') {
            lines++;
        }
        text++;
    }

    return lines;
}

TEST(interactive_manual_matches_language) {
    const char *en = manual_text_interactive(LANG_EN);
    const char *zh = manual_text_interactive(LANG_ZH);

    assert(strstr(en, "TNT(1) help") != NULL);
    assert(strstr(en, "Use") != NULL);
    assert(strstr(en, "Commands") != NULL);
    assert(strstr(en, ":lang en|zh") != NULL);
    assert(strstr(en, ":mute-joins") != NULL);
    assert(strstr(en, ":support") == NULL);
    assert(strstr(en, ":commands") == NULL);
    assert(count_lines(en) <= 20);

    assert(strstr(zh, "TNT(1) 帮助") != NULL);
    assert(strstr(zh, "使用") != NULL);
    assert(strstr(zh, "命令") != NULL);
    assert(strstr(zh, ":lang en|zh") != NULL);
    assert(strstr(zh, ":mute-joins") != NULL);
    assert(strstr(zh, ":support") == NULL);
    assert(strstr(zh, ":commands") == NULL);
    assert(count_lines(zh) <= 20);
}

int main(void) {
    printf("Running manual text unit tests...\n\n");

    RUN_TEST(interactive_manual_matches_language);

    printf("\n✓ All %d tests passed!\n", tests_passed);
    return 0;
}
