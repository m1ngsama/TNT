/* Unit tests for command catalog names, aliases, and generated help text */

#include "../../include/command_catalog.h"
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

    assert(command_catalog_match("reply hello back", &id, &args));
    assert(id == TNT_COMMAND_REPLY);
    assert(strcmp(args, "hello back") == 0);

    assert(command_catalog_match("r hello back", &id, &args));
    assert(id == TNT_COMMAND_REPLY);
    assert(strcmp(args, "hello back") == 0);

    assert(command_catalog_match("language zh", &id, &args));
    assert(id == TNT_COMMAND_LANG);
    assert(strcmp(args, "zh") == 0);
}

TEST(matches_known_commands_before_argument_validation) {
    tnt_command_id_t id;
    const char *args;

    assert(command_catalog_match("users extra", &id, &args));
    assert(id == TNT_COMMAND_USERS);
    assert(strcmp(args, "extra") == 0);

    assert(command_catalog_match("help now", &id, &args));
    assert(id == TNT_COMMAND_HELP);
    assert(strcmp(args, "now") == 0);

    assert(command_catalog_match("q now", &id, &args));
    assert(id == TNT_COMMAND_QUIT);
    assert(strcmp(args, "now") == 0);
}

TEST(validates_argument_shapes) {
    assert(command_catalog_args_valid(TNT_COMMAND_USERS, NULL));
    assert(!command_catalog_args_valid(TNT_COMMAND_USERS, "extra"));
    assert(command_catalog_args_valid(TNT_COMMAND_HELP, NULL));
    assert(!command_catalog_args_valid(TNT_COMMAND_HELP, "now"));

    assert(!command_catalog_args_valid(TNT_COMMAND_MSG, NULL));
    assert(command_catalog_args_valid(TNT_COMMAND_MSG, "alice hello"));
    assert(!command_catalog_args_valid(TNT_COMMAND_REPLY, ""));
    assert(command_catalog_args_valid(TNT_COMMAND_REPLY, "hello back"));
    assert(!command_catalog_args_valid(TNT_COMMAND_SEARCH, ""));
    assert(command_catalog_args_valid(TNT_COMMAND_SEARCH, "needle"));

    assert(command_catalog_args_valid(TNT_COMMAND_LAST, NULL));
    assert(command_catalog_args_valid(TNT_COMMAND_LAST, "999"));
    assert(command_catalog_args_valid(TNT_COMMAND_LANG, "fr"));
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
    assert(strstr(en, ":reply <message>") != NULL);
    assert(strstr(en, "Show private messages") != NULL);
    assert(strstr(en, ":support") == NULL);

    assert(strstr(zh, ":users, :list, :who") != NULL);
    assert(strstr(zh, "显示在线用户") != NULL);
    assert(strstr(zh, "查看私信") != NULL);
    assert(strstr(zh, ":msg <user> <message>") != NULL);
    assert(strstr(zh, ":reply <message>") != NULL);
    assert(strstr(zh, "<用户>") == NULL);
    assert(strstr(zh, "<消息>") == NULL);
    assert(strstr(zh, ":support") == NULL);
    assert_ascii_angle_placeholders(zh);
}

TEST(generates_localized_usage) {
    char en[256] = {0};
    char zh[256] = {0};
    size_t en_pos = 0;
    size_t zh_pos = 0;

    command_catalog_append_usage(en, sizeof(en), &en_pos,
                                 TNT_COMMAND_LAST, UI_LANG_EN);
    command_catalog_append_usage(zh, sizeof(zh), &zh_pos,
                                 TNT_COMMAND_MSG, UI_LANG_ZH);

    assert(strcmp(en, "Usage: last [N]  (N: 1-50, default 10)\n") == 0);
    assert(strcmp(zh, "用法: msg <user> <message>\n"
                      "      w <user> <message>\n") == 0);

    en[0] = '\0';
    en_pos = 0;
    command_catalog_append_usage(en, sizeof(en), &en_pos,
                                 TNT_COMMAND_REPLY, UI_LANG_EN);
    assert(strcmp(en, "Usage: reply <message>\n"
                      "       r <message>\n") == 0);

    en[0] = '\0';
    en_pos = 0;
    command_catalog_append_usage(en, sizeof(en), &en_pos,
                                 TNT_COMMAND_USERS, (ui_lang_t)99);
    assert(strcmp(en, "Usage: users\n") == 0);
}

int main(void) {
    printf("Running command catalog unit tests...\n\n");

    RUN_TEST(matches_canonical_names_and_aliases);
    RUN_TEST(matches_known_commands_before_argument_validation);
    RUN_TEST(validates_argument_shapes);
    RUN_TEST(suggests_from_catalog_aliases);
    RUN_TEST(generates_localized_help_sections);
    RUN_TEST(generates_localized_usage);

    printf("\n✓ All %d tests passed!\n", tests_passed);
    return 0;
}
