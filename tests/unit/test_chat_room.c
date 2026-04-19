/* Unit tests for chat_room functions */

/* Minimal client_t stub — only pointer identity matters for add/remove.
 * We define `struct client` before including chat_room.h so the forward
 * declaration resolves without pulling in ssh_server.h / libssh. */
#include "../../include/common.h"

struct client {
    char username[MAX_USERNAME_LEN];
    int dummy;
};
typedef struct client client_t;

#include "../../include/chat_room.h"
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

static message_t make_msg(const char *user, const char *content) {
    message_t m = { .timestamp = time(NULL) };
    strncpy(m.username, user, MAX_USERNAME_LEN - 1);
    strncpy(m.content, content, MAX_MESSAGE_LEN - 1);
    return m;
}

TEST(room_create_destroy) {
    chat_room_t *room = room_create();
    assert(room != NULL);
    assert(room->client_count == 0);
    assert(room->client_capacity > 0);
    room_destroy(room);
}

TEST(room_add_message_single) {
    chat_room_t *room = room_create();
    message_t msg = make_msg("alice", "hello");

    room_broadcast(room, &msg);
    assert(room->message_count == 1);
    assert(strcmp(room->messages[0].username, "alice") == 0);
    assert(strcmp(room->messages[0].content, "hello") == 0);

    room_destroy(room);
}

TEST(room_add_message_overflow) {
    chat_room_t *room = room_create();

    for (int i = 0; i < MAX_MESSAGES + 10; i++) {
        char content[32];
        snprintf(content, sizeof(content), "msg %d", i);
        message_t msg = make_msg("user", content);
        room_broadcast(room, &msg);
    }

    assert(room->message_count == MAX_MESSAGES);

    char expected[32];
    snprintf(expected, sizeof(expected), "msg %d", 10);
    assert(strcmp(room->messages[0].content, expected) == 0);

    snprintf(expected, sizeof(expected), "msg %d", MAX_MESSAGES + 9);
    assert(strcmp(room->messages[MAX_MESSAGES - 1].content, expected) == 0);

    room_destroy(room);
}

TEST(room_broadcast_increments_seq) {
    chat_room_t *room = room_create();
    g_room = room;

    uint64_t seq1 = room_get_update_seq(room);
    message_t msg = make_msg("bob", "hi");
    room_broadcast(room, &msg);
    uint64_t seq2 = room_get_update_seq(room);

    assert(seq2 > seq1);
    assert(room_get_message_count(room) == 1);

    g_room = NULL;
    room_destroy(room);
}

TEST(room_get_message_valid) {
    chat_room_t *room = room_create();
    message_t msg = make_msg("carol", "test");
    room_broadcast(room, &msg);

    message_t out;
    assert(room_get_message(room, 0, &out) == true);
    assert(strcmp(out.username, "carol") == 0);
    assert(strcmp(out.content, "test") == 0);

    room_destroy(room);
}

TEST(room_get_message_invalid_index) {
    chat_room_t *room = room_create();

    message_t out;
    assert(room_get_message(room, 0, &out) == false);
    assert(room_get_message(room, -1, &out) == false);
    assert(room_get_message(room, 999, &out) == false);

    room_destroy(room);
}

TEST(room_get_message_null_args) {
    chat_room_t *room = room_create();
    message_t out;

    assert(room_get_message(NULL, 0, &out) == false);
    assert(room_get_message(room, 0, NULL) == false);

    room_destroy(room);
}

TEST(room_client_count) {
    chat_room_t *room = room_create();
    assert(room_get_client_count(room) == 0);

    client_t c1 = {0};
    client_t c2 = {0};
    assert(room_add_client(room, &c1) == 0);
    assert(room_get_client_count(room) == 1);
    assert(room_add_client(room, &c2) == 0);
    assert(room_get_client_count(room) == 2);

    room_remove_client(room, &c1);
    assert(room_get_client_count(room) == 1);

    room_remove_client(room, &c2);
    assert(room_get_client_count(room) == 0);

    room_destroy(room);
}

TEST(room_remove_nonexistent_client) {
    chat_room_t *room = room_create();
    client_t c1 = {0};
    client_t c2 = {0};

    room_add_client(room, &c1);
    room_remove_client(room, &c2);
    assert(room_get_client_count(room) == 1);

    room_destroy(room);
}

TEST(room_add_client_full) {
    chat_room_t *room = room_create();
    client_t clients[MAX_CLIENTS + 1];
    memset(clients, 0, sizeof(clients));

    for (int i = 0; i < room->client_capacity; i++) {
        assert(room_add_client(room, &clients[i]) == 0);
    }

    assert(room_add_client(room, &clients[room->client_capacity]) == -1);
    assert(room_get_client_count(room) == room->client_capacity);

    room_destroy(room);
}

TEST(room_message_count_threadsafe) {
    chat_room_t *room = room_create();

    assert(room_get_message_count(room) == 0);

    message_t msg = make_msg("dave", "msg");
    room_broadcast(room, &msg);
    assert(room_get_message_count(room) == 1);

    room_broadcast(room, &msg);
    room_broadcast(room, &msg);
    assert(room_get_message_count(room) == 3);

    room_destroy(room);
}

int main(void) {
    printf("=== Chat Room Unit Tests ===\n");

    RUN_TEST(room_create_destroy);
    RUN_TEST(room_add_message_single);
    RUN_TEST(room_add_message_overflow);
    RUN_TEST(room_broadcast_increments_seq);
    RUN_TEST(room_get_message_valid);
    RUN_TEST(room_get_message_invalid_index);
    RUN_TEST(room_get_message_null_args);
    RUN_TEST(room_client_count);
    RUN_TEST(room_remove_nonexistent_client);
    RUN_TEST(room_add_client_full);
    RUN_TEST(room_message_count_threadsafe);

    printf("\nAll %d tests passed!\n", tests_passed);
    return 0;
}
