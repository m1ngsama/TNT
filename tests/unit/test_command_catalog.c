/* Unit tests for command catalog names, aliases, and generated help text */

#include "../../include/command_catalog.h"
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

TEST(matches_canonical_names_and_aliases) {
    tnt_command_id_t id;
    const char *args;

    assert(command_catalog_match("users", &id, &args));
    assert(id == TNT_COMMAND_USERS);
    assert(strcmp(args, "") == 0);

    assert(command_catalog_match("list", &id, &args));
    assert(id == TNT_COMMAND_USERS);

    assert(command_catalog_match("msg alice hello", &id, &args));
    assert(id == TNT_COMMAND_MSG);
    assert(strcmp(args, "alice hello") == 0);

    assert(command_catalog_match("w alice hello", &id, &args));
    assert(id == TNT_COMMAND_MSG);
    assert(strcmp(args, "alice hello") == 0);

    assert(command_catalog_match("language zh", &id, &args));
    assert(id == TNT_COMMAND_LANG);
    assert(strcmp(args, "zh") == 0);
}

TEST(rejects_arguments_for_no_arg_commands) {
    tnt_command_id_t id;
    const char *args;

    assert(!command_catalog_match("users extra", &id, &args));
    assert(!command_catalog_match("help now", &id, &args));
    assert(!command_catalog_match("q now", &id, &args));
}

TEST(suggests_from_catalog_aliases) {
    assert(strcmp(command_catalog_suggest("hlep"), "help") == 0);
    assert(strcmp(command_catalog_suggest("usres"), "users") == 0);
    assert(strcmp(command_catalog_suggest("laguage"), "lang") == 0);
    assert(command_catalog_suggest("not-even-close") == NULL);
}

TEST(generates_localized_help_sections) {
    char en[4096] = {0};
    char zh[4096] = {0};
    size_t en_pos = 0;
    size_t zh_pos = 0;

    command_catalog_append_full(en, sizeof(en), &en_pos, UI_LANG_EN);
    command_catalog_append_full(zh, sizeof(zh), &zh_pos, UI_LANG_ZH);

    assert(strstr(en, ":users, :list, :who") != NULL);
    assert(strstr(en, "Show online users") != NULL);
    assert(strstr(en, ":msg <user> <message>") != NULL);
    assert(strstr(en, ":support") == NULL);

    assert(strstr(zh, ":users, :list, :who") != NULL);
    assert(strstr(zh, "显示在线用户") != NULL);
    assert(strstr(zh, ":msg <user> <message>") != NULL);
    assert(strstr(zh, "<用户>") == NULL);
    assert(strstr(zh, "<消息>") == NULL);
    assert(strstr(zh, ":support") == NULL);
}

int main(void) {
    printf("Running command catalog unit tests...\n\n");

    RUN_TEST(matches_canonical_names_and_aliases);
    RUN_TEST(rejects_arguments_for_no_arg_commands);
    RUN_TEST(suggests_from_catalog_aliases);
    RUN_TEST(generates_localized_help_sections);

    printf("\n✓ All %d tests passed!\n", tests_passed);
    return 0;
}
