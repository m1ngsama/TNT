/* Unit tests for keymap selection */
#include "../../include/keymap.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define TEST(name) static void test_##name()
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    test_##name(); \
    printf("✓\n"); \
    tests_passed++; \
} while(0)

static int tests_passed = 0;

TEST(keymap_names_round_trip) {
    assert(tnt_keymap_from_name("vim", TNT_KEYMAP_DEFAULT) == TNT_KEYMAP_VIM);
    assert(tnt_keymap_from_name("default", TNT_KEYMAP_VIM) == TNT_KEYMAP_DEFAULT);
    assert(strcmp(tnt_keymap_name(TNT_KEYMAP_VIM), "vim") == 0);
    assert(strcmp(tnt_keymap_name(TNT_KEYMAP_DEFAULT), "default") == 0);
}

TEST(keymap_unknown_name_keeps_the_fallback) {
    assert(tnt_keymap_from_name("emacs", TNT_KEYMAP_DEFAULT) == TNT_KEYMAP_DEFAULT);
    assert(tnt_keymap_from_name("", TNT_KEYMAP_VIM) == TNT_KEYMAP_VIM);
    assert(tnt_keymap_from_name(NULL, TNT_KEYMAP_VIM) == TNT_KEYMAP_VIM);
}

TEST(keymap_login_name_selects_vim) {
    assert(tnt_keymap_from_login("vim", TNT_KEYMAP_DEFAULT) == TNT_KEYMAP_VIM);
    assert(tnt_keymap_from_login("VIM", TNT_KEYMAP_DEFAULT) == TNT_KEYMAP_VIM);
    /* An ordinary login must not change the keymap. */
    assert(tnt_keymap_from_login("alice", TNT_KEYMAP_DEFAULT) == TNT_KEYMAP_DEFAULT);
    assert(tnt_keymap_from_login("vimmer", TNT_KEYMAP_DEFAULT) == TNT_KEYMAP_DEFAULT);
    assert(tnt_keymap_from_login("", TNT_KEYMAP_DEFAULT) == TNT_KEYMAP_DEFAULT);
    assert(tnt_keymap_from_login(NULL, TNT_KEYMAP_VIM) == TNT_KEYMAP_VIM);
}

TEST(keymap_only_vim_uses_modes) {
    assert(tnt_keymap_uses_modes(TNT_KEYMAP_VIM));
    assert(!tnt_keymap_uses_modes(TNT_KEYMAP_DEFAULT));
}

int main(void) {
    printf("Running keymap unit tests...\n\n");
    RUN_TEST(keymap_names_round_trip);
    RUN_TEST(keymap_unknown_name_keeps_the_fallback);
    RUN_TEST(keymap_login_name_selects_vim);
    RUN_TEST(keymap_only_vim_uses_modes);
    printf("\nAll %d keymap tests passed!\n", tests_passed);
    return 0;
}
