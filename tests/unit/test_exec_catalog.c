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
    assert(strstr(en, "post MESSAGE") != NULL);
    assert(strstr(en, "support") == NULL);

    assert(strstr(zh, "TNT exec 接口") != NULL);
    assert(strstr(zh, "命令:") != NULL);
    assert(strstr(zh, "users [--json]") != NULL);
    assert(strstr(zh, "post MESSAGE") != NULL);
    assert(strstr(zh, "support") == NULL);
    assert_ascii_angle_placeholders(zh);
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

    assert(exec_catalog_match("post hello world", &id, &args));
    assert(id == TNT_EXEC_COMMAND_POST);
    assert(strcmp(args, "hello world") == 0);

    assert(exec_catalog_match("exit", &id, &args));
    assert(id == TNT_EXEC_COMMAND_EXIT);
    assert(args == NULL);

    assert(!exec_catalog_match("usersx", &id, &args));
    assert(!exec_catalog_match("nope", &id, &args));
}

int main(void) {
    printf("Running exec catalog unit tests...\n\n");

    RUN_TEST(generates_localized_exec_help);
    RUN_TEST(matches_exec_commands_and_args);

    printf("\n✓ All %d tests passed!\n", tests_passed);
    return 0;
}
