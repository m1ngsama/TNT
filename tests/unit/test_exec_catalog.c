/* Unit tests for SSH exec command help catalog */

#include "../../include/exec_catalog.h"
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

TEST(generates_localized_exec_help) {
    char en[2048] = {0};
    char zh[2048] = {0};
    size_t en_pos = 0;
    size_t zh_pos = 0;

    exec_catalog_append_help(en, sizeof(en), &en_pos, UI_LANG_EN);
    exec_catalog_append_help(zh, sizeof(zh), &zh_pos, UI_LANG_ZH);

    assert(strstr(en, "TNT exec interface") != NULL);
    assert(strstr(en, "Commands:") != NULL);
    assert(strstr(en, "users [--json]") != NULL);
    assert(strstr(en, "dump [N]") != NULL);
    assert(strstr(en, "post MESSAGE") != NULL);
    assert(strstr(en, "support") == NULL);

    assert(strstr(zh, "TNT exec 接口") != NULL);
    assert(strstr(zh, "命令:") != NULL);
    assert(strstr(zh, "users [--json]") != NULL);
    assert(strstr(zh, "dump [N]") != NULL);
    assert(strstr(zh, "post MESSAGE") != NULL);
    assert(strstr(zh, "support") == NULL);
    assert_ascii_angle_placeholders(zh);

    en[0] = '\0';
    en_pos = 0;
    exec_catalog_append_help(en, sizeof(en), &en_pos, (ui_lang_t)99);
    assert(strstr(en, "TNT exec interface") != NULL);
    assert(strstr(en, "Show this help") != NULL);
}

TEST(matches_exec_commands_and_args) {
    tnt_exec_command_id_t id;
    const char *args;

    assert(exec_catalog_match("help", &id, &args));
    assert(id == TNT_EXEC_COMMAND_HELP);
    assert(args == NULL);

    assert(exec_catalog_match("--help", &id, &args));
    assert(id == TNT_EXEC_COMMAND_HELP);
    assert(args == NULL);

    assert(exec_catalog_match("users --json", &id, &args));
    assert(id == TNT_EXEC_COMMAND_USERS);
    assert(strcmp(args, "--json") == 0);

    assert(exec_catalog_match("tail -n 20", &id, &args));
    assert(id == TNT_EXEC_COMMAND_TAIL);
    assert(strcmp(args, "-n 20") == 0);

    assert(exec_catalog_match("dump -n 20", &id, &args));
    assert(id == TNT_EXEC_COMMAND_DUMP);
    assert(strcmp(args, "-n 20") == 0);

    assert(exec_catalog_match("post hello world", &id, &args));
    assert(id == TNT_EXEC_COMMAND_POST);
    assert(strcmp(args, "hello world") == 0);

    assert(exec_catalog_match("exit", &id, &args));
    assert(id == TNT_EXEC_COMMAND_EXIT);
    assert(args == NULL);

    assert(!exec_catalog_match("usersx", &id, &args));
    assert(!exec_catalog_match("nope", &id, &args));
}

TEST(validates_argument_shapes) {
    assert(exec_catalog_args_valid(TNT_EXEC_COMMAND_HELP, NULL));
    assert(!exec_catalog_args_valid(TNT_EXEC_COMMAND_HELP, "now"));
    assert(exec_catalog_args_valid(TNT_EXEC_COMMAND_HEALTH, NULL));
    assert(!exec_catalog_args_valid(TNT_EXEC_COMMAND_HEALTH, "now"));

    assert(exec_catalog_args_valid(TNT_EXEC_COMMAND_USERS, NULL));
    assert(exec_catalog_args_valid(TNT_EXEC_COMMAND_USERS, "--json"));
    assert(!exec_catalog_args_valid(TNT_EXEC_COMMAND_USERS, "--xml"));

    assert(exec_catalog_args_valid(TNT_EXEC_COMMAND_TAIL, NULL));
    assert(exec_catalog_args_valid(TNT_EXEC_COMMAND_TAIL, "-n 20"));

    assert(exec_catalog_args_valid(TNT_EXEC_COMMAND_DUMP, NULL));
    assert(exec_catalog_args_valid(TNT_EXEC_COMMAND_DUMP, "-n 20"));

    assert(!exec_catalog_args_valid(TNT_EXEC_COMMAND_POST, NULL));
    assert(exec_catalog_args_valid(TNT_EXEC_COMMAND_POST, "hello"));
}

TEST(generates_localized_usage) {
    char en[256] = {0};
    char zh[256] = {0};
    size_t en_pos = 0;
    size_t zh_pos = 0;

    exec_catalog_append_usage(en, sizeof(en), &en_pos,
                              TNT_EXEC_COMMAND_TAIL, UI_LANG_EN);
    exec_catalog_append_usage(zh, sizeof(zh), &zh_pos,
                              TNT_EXEC_COMMAND_POST, UI_LANG_ZH);

    assert(strcmp(en, "tail: usage: tail [N] | tail -n N\n") == 0);
    assert(strcmp(zh, "post: 用法: post MESSAGE\n") == 0);

    memset(en, 0, sizeof(en));
    en_pos = 0;
    exec_catalog_append_usage(en, sizeof(en), &en_pos,
                              TNT_EXEC_COMMAND_DUMP, (ui_lang_t)99);
    assert(strcmp(en, "dump: usage: dump [N] | dump -n N\n") == 0);
}

TEST(generates_unique_command_list) {
    char output[256] = {0};
    size_t pos = 0;

    exec_catalog_append_command_list(output, sizeof(output), &pos);

    assert(strcmp(output,
                  "help, health, users, stats, tail, dump, post, exit") == 0);
}

int main(void) {
    printf("Running exec catalog unit tests...\n\n");

    RUN_TEST(generates_localized_exec_help);
    RUN_TEST(matches_exec_commands_and_args);
    RUN_TEST(validates_argument_shapes);
    RUN_TEST(generates_localized_usage);
    RUN_TEST(generates_unique_command_list);

    printf("\n✓ All %d tests passed!\n", tests_passed);
    return 0;
}
