/* Unit tests for localized system event messages */

#include "../../include/system_message.h"
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

TEST(join_leave_follow_language) {
    message_t msg;

    system_message_make_join(&msg, "alice", UI_LANG_ZH);
    assert(strcmp(msg.username, "系统") == 0);
    assert(strstr(msg.content, "alice") != NULL);
    assert(strstr(msg.content, "加入了聊天室") != NULL);
    assert(system_message_is_system(&msg));
    assert(system_message_is_join_leave(&msg));

    system_message_make_leave(&msg, "bob", UI_LANG_EN);
    assert(strcmp(msg.username, "system") == 0);
    assert(strstr(msg.content, "bob") != NULL);
    assert(strstr(msg.content, "left the room") != NULL);
    assert(system_message_is_system(&msg));
    assert(system_message_is_join_leave(&msg));
}

TEST(nick_messages_are_system_events_not_join_leave) {
    message_t msg;

    system_message_make_nick(&msg, "old", "new", UI_LANG_EN);
    assert(strcmp(msg.username, "system") == 0);
    assert(strstr(msg.content, "old") != NULL);
    assert(strstr(msg.content, "new") != NULL);
    assert(strstr(msg.content, "renamed") != NULL);
    assert(system_message_is_system(&msg));
    assert(!system_message_is_join_leave(&msg));

    system_message_make_nick(&msg, "旧", "新", UI_LANG_ZH);
    assert(strcmp(msg.username, "系统") == 0);
    assert(strstr(msg.content, "更名为") != NULL);
    assert(system_message_is_system(&msg));
    assert(!system_message_is_join_leave(&msg));
}

TEST(legacy_system_names_are_recognized) {
    message_t msg = {0};

    snprintf(msg.username, sizeof(msg.username), "系统");
    snprintf(msg.content, sizeof(msg.content), "alice 离开了聊天室");
    assert(system_message_is_system(&msg));
    assert(system_message_is_join_leave(&msg));

    snprintf(msg.username, sizeof(msg.username), "system");
    snprintf(msg.content, sizeof(msg.content), "alice joined the room");
    assert(system_message_is_system(&msg));
    assert(system_message_is_join_leave(&msg));
}

int main(void) {
    printf("Running system message unit tests...\n\n");

    RUN_TEST(join_leave_follow_language);
    RUN_TEST(nick_messages_are_system_events_not_join_leave);
    RUN_TEST(legacy_system_names_are_recognized);

    printf("\n✓ All %d tests passed!\n", tests_passed);
    return 0;
}
