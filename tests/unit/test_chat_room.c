/* Unit tests for chat_room functions */

/* Minimal client_t stub — only pointer identity matters for add/remove.
 * We define `struct client` before including chat_room.h so the forward
 * declaration resolves without pulling in ssh_server.h / libssh. */
#include "../../include/common.h"

struct client {
    char username[MAX_USERNAME_LEN];
    int wake_count;
    uint64_t expected_seq;
    uint64_t rendered_seq;
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

static void count_client_wake(struct client *client, uint64_t seq) {
    client->wake_count++;
    client->expected_seq = seq;
}

static const char *test_client_name(const struct client *client) {
    return client ? client->username : NULL;
}

static bool record_client_render(struct client *client, uint64_t seq) {
    if (client->expected_seq != seq) return false;
    if (client->rendered_seq == seq) return false;
    client->rendered_seq = seq;
    return true;
}

static void assert_message_content(chat_room_t *room, int index,
                                   const char *expected) {
    message_t message;

    assert(room_get_message(room, index, &message));
    assert(strcmp(message.content, expected) == 0);
}

typedef struct {
    chat_room_t *room;
    atomic_bool writer_done;
    atomic_bool failed;
} concurrent_history_test_t;

static void *history_writer(void *arg) {
    concurrent_history_test_t *test = arg;

    for (int i = 0; i < 5000; i++) {
        char content[32];
        snprintf(content, sizeof(content), "msg %d", i);
        message_t msg = make_msg("writer", content);
        room_broadcast(test->room, &msg);
    }
    atomic_store(&test->writer_done, true);
    return NULL;
}

static void *history_reader(void *arg) {
    concurrent_history_test_t *test = arg;
    message_t snapshot[MAX_MESSAGES];

    do {
        int count = room_copy_messages(test->room, 0, snapshot, MAX_MESSAGES);
        int previous = -1;

        for (int i = 0; i < count; i++) {
            int current;
            if (sscanf(snapshot[i].content, "msg %d", &current) != 1 ||
                (i > 0 && current != previous + 1)) {
                atomic_store(&test->failed, true);
                return NULL;
            }
            previous = current;
        }
    } while (!atomic_load(&test->writer_done));

    return NULL;
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
    assert(room_get_message_count(room) == 1);

    message_t stored;
    assert(room_get_message(room, 0, &stored));
    assert(strcmp(stored.username, "alice") == 0);
    assert(strcmp(stored.content, "hello") == 0);

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

    assert(room_get_message_count(room) == MAX_MESSAGES);
    assert(room->message_history.start == 10);

    for (int i = 0; i < MAX_MESSAGES; i++) {
        char expected[32];
        snprintf(expected, sizeof(expected), "msg %d", i + 10);
        assert_message_content(room, i, expected);
    }

    room_destroy(room);
}

TEST(room_add_message_multiple_wraps) {
    chat_room_t *room = room_create();
    const int total = MAX_MESSAGES * 3 + 17;

    for (int i = 0; i < total; i++) {
        char content[32];
        snprintf(content, sizeof(content), "msg %d", i);
        message_t msg = make_msg("user", content);
        room_broadcast(room, &msg);
    }

    assert(room_get_message_count(room) == MAX_MESSAGES);
    assert(room->message_history.start == 17);
    for (int i = 0; i < MAX_MESSAGES; i++) {
        char expected[32];
        snprintf(expected, sizeof(expected), "msg %d",
                 total - MAX_MESSAGES + i);
        assert_message_content(room, i, expected);
    }

    room_destroy(room);
}

TEST(room_copy_messages_across_wrap) {
    chat_room_t *room = room_create();

    for (int i = 0; i < MAX_MESSAGES + 7; i++) {
        char content[32];
        snprintf(content, sizeof(content), "msg %d", i);
        message_t msg = make_msg("user", content);
        room_broadcast(room, &msg);
    }

    message_t slice[8];
    int copied = room_copy_messages(room, 90, slice, 8);
    assert(copied == 8);
    for (int i = 0; i < copied; i++) {
        char expected[32];
        snprintf(expected, sizeof(expected), "msg %d", 97 + i);
        assert(strcmp(slice[i].content, expected) == 0);
    }

    room_destroy(room);
}

TEST(room_copy_recent_messages_after_wrap) {
    chat_room_t *room = room_create();

    for (int i = 0; i < MAX_MESSAGES + 7; i++) {
        char content[32];
        snprintf(content, sizeof(content), "msg %d", i);
        message_t msg = make_msg("user", content);
        room_broadcast(room, &msg);
    }

    message_t recent[5];
    int total_count = -1;
    int copied = room_copy_recent_messages(room, recent, 5, &total_count);
    assert(total_count == MAX_MESSAGES);
    assert(copied == 5);
    for (int i = 0; i < copied; i++) {
        char expected[32];
        snprintf(expected, sizeof(expected), "msg %d", 102 + i);
        assert(strcmp(recent[i].content, expected) == 0);
    }

    room_destroy(room);
}

TEST(room_copy_messages_invalid_args) {
    chat_room_t *room = room_create();
    message_t message;
    int total_count = 123;

    assert(room_copy_messages(NULL, 0, &message, 1) == 0);
    assert(room_copy_messages(room, -1, &message, 1) == 0);
    assert(room_copy_messages(room, 0, NULL, 1) == 0);
    assert(room_copy_messages(room, 0, &message, 0) == 0);
    assert(room_copy_recent_messages(NULL, &message, 1, &total_count) == 0);
    assert(total_count == 0);

    room_destroy(room);
}

TEST(room_concurrent_snapshots_stay_ordered) {
    chat_room_t *room = room_create();
    concurrent_history_test_t test = {
        .room = room,
        .writer_done = ATOMIC_VAR_INIT(false),
        .failed = ATOMIC_VAR_INIT(false),
    };
    pthread_t writer;
    pthread_t reader;

    assert(pthread_create(&writer, NULL, history_writer, &test) == 0);
    assert(pthread_create(&reader, NULL, history_reader, &test) == 0);
    assert(pthread_join(writer, NULL) == 0);
    assert(pthread_join(reader, NULL) == 0);
    assert(!atomic_load(&test.failed));

    for (int i = 0; i < MAX_MESSAGES; i++) {
        char expected[32];
        snprintf(expected, sizeof(expected), "msg %d", 4900 + i);
        assert_message_content(room, i, expected);
    }

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

TEST(room_broadcast_notifies_current_clients) {
    chat_room_t *room = room_create();
    client_t c1 = {0};
    client_t c2 = {0};
    message_t msg = make_msg("alice", "wake everyone");

    /* The notifier is optional, preserving the standalone room default. */
    room_broadcast(room, &msg);
    assert(c1.wake_count == 0);

    room_set_client_notifier(room, count_client_wake);
    assert(room_add_client(room, &c1) == 0);
    assert(room_add_client(room, &c2) == 0);
    room_broadcast(room, &msg);
    assert(c1.wake_count == 1);
    assert(c2.wake_count == 1);

    room_remove_client(room, &c1);
    room_broadcast(room, &msg);
    assert(c1.wake_count == 1);
    assert(c2.wake_count == 2);

    room_destroy(room);
}

TEST(room_distribution_completion_is_deduplicated) {
    chat_room_t *room = room_create();
    client_t c1 = {0};
    client_t c2 = {0};
    message_t msg = make_msg("alice", "measure distribution");
    room_distribution_stats_t stats;

    room_set_client_notifier(room, count_client_wake);
    room_set_client_render_ack(room, record_client_render);
    assert(room_add_client(room, &c1) == 0);
    assert(room_add_client(room, &c2) == 0);
    room_broadcast(room, &msg);
    room_get_distribution_stats(room, &stats);
    assert(stats.current_seq > 0);
    assert(stats.expected_clients == 2);
    assert(stats.completed_clients == 0);

    room_record_client_rendered(room, &c1, stats.current_seq);
    room_record_client_rendered(room, &c1, stats.current_seq);
    room_get_distribution_stats(room, &stats);
    assert(stats.completed_clients == 1);
    assert(stats.last_complete_seq == 0);

    /* A client that joined after this generation was published cannot
     * substitute for one of the two clients counted at broadcast time. */
    client_t late = {0};
    assert(room_add_client(room, &late) == 0);
    room_record_client_rendered(room, &late, stats.current_seq);
    room_get_distribution_stats(room, &stats);
    assert(stats.completed_clients == 1);

    room_record_client_rendered(room, &c2, stats.current_seq);
    room_get_distribution_stats(room, &stats);
    assert(stats.completed_clients == 2);
    assert(stats.last_complete_seq == stats.current_seq);

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
    snprintf(c1.username, sizeof(c1.username), "alice");
    snprintf(c2.username, sizeof(c2.username), "bob");
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
    client_t *clients = calloc((size_t)room->client_capacity + 1,
                               sizeof(*clients));
    assert(clients != NULL);

    for (int i = 0; i < room->client_capacity; i++) {
        snprintf(clients[i].username, sizeof(clients[i].username),
                 "client-%d", i);
        assert(room_add_client(room, &clients[i]) == 0);
    }

    snprintf(clients[room->client_capacity].username,
             sizeof(clients[room->client_capacity].username), "overflow");
    assert(room_add_client(room, &clients[room->client_capacity]) == -1);
    assert(room_get_client_count(room) == room->client_capacity);

    free(clients);
    room_destroy(room);
}

TEST(room_capacity_follows_tnt_max_connections) {
    setenv("TNT_MAX_CONNECTIONS", "3", 1);
    chat_room_t *room = room_create();
    unsetenv("TNT_MAX_CONNECTIONS");
    client_t clients[4];
    memset(clients, 0, sizeof(clients));
    for (int i = 0; i < 4; i++) {
        snprintf(clients[i].username, sizeof(clients[i].username),
                 "client-%d", i);
    }

    assert(room->client_capacity == 3);
    assert(room_add_client(room, &clients[0]) == 0);
    assert(room_add_client(room, &clients[1]) == 0);
    assert(room_add_client(room, &clients[2]) == 0);
    assert(room_add_client(room, &clients[3]) == -1);

    room_destroy(room);
}

TEST(room_rejects_duplicate_names_case_insensitively) {
    chat_room_t *room = room_create();
    client_t first = {0};
    client_t duplicate = {0};

    snprintf(first.username, sizeof(first.username), "Alice");
    snprintf(duplicate.username, sizeof(duplicate.username), "alice");
    room_set_client_name_accessor(room, test_client_name);

    assert(room_add_client(room, &first) == 0);
    assert(room_add_client(room, &duplicate) == -2);
    assert(room_get_client_count(room) == 1);

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
    RUN_TEST(room_add_message_multiple_wraps);
    RUN_TEST(room_copy_messages_across_wrap);
    RUN_TEST(room_copy_recent_messages_after_wrap);
    RUN_TEST(room_copy_messages_invalid_args);
    RUN_TEST(room_concurrent_snapshots_stay_ordered);
    RUN_TEST(room_broadcast_increments_seq);
    RUN_TEST(room_broadcast_notifies_current_clients);
    RUN_TEST(room_distribution_completion_is_deduplicated);
    RUN_TEST(room_get_message_valid);
    RUN_TEST(room_get_message_invalid_index);
    RUN_TEST(room_get_message_null_args);
    RUN_TEST(room_client_count);
    RUN_TEST(room_remove_nonexistent_client);
    RUN_TEST(room_add_client_full);
    RUN_TEST(room_capacity_follows_tnt_max_connections);
    RUN_TEST(room_rejects_duplicate_names_case_insensitively);
    RUN_TEST(room_message_count_threadsafe);

    printf("\nAll %d tests passed!\n", tests_passed);
    return 0;
}
