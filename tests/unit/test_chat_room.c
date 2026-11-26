/* Unit tests for chat_room functions */

/* Minimal client_t stub — only pointer identity matters for add/remove.
 * We define `struct client` before including chat_room.h so the forward
 * declaration resolves without pulling in ssh_server.h / libssh. */
#include "../../include/common.h"

struct chat_room;
struct client {
    char username[MAX_USERNAME_LEN];
    int wake_count;
    const struct chat_room *room;
};
typedef struct client client_t;

#include "../../include/chat_room.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

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

static void count_client_wake(struct client *client) {
    client->wake_count++;
}

static const char *test_client_name(const struct client *client) {
    return client ? client->username : NULL;
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

static char presence_log[256];

static void record_presence(const char *nickname, const char *room,
                            bool joined) {
    size_t len = strlen(presence_log);

    snprintf(presence_log + len, sizeof(presence_log) - len, "%s%s@%s;",
             joined ? "+" : "-", nickname, room);
}

TEST(room_reports_presence_changes) {
    chat_room_t *room = room_create();
    client_t alice = {0};
    client_t duplicate = {0};
    client_t bob = {0};

    snprintf(alice.username, sizeof(alice.username), "alice");
    snprintf(duplicate.username, sizeof(duplicate.username), "ALICE");
    snprintf(bob.username, sizeof(bob.username), "bob");
    presence_log[0] = '\0';
    room_set_client_name_accessor(room, test_client_name);
    room_set_presence_notifier(room, record_presence);

    assert(room_add_client(room, &alice) == 0);
    assert(room_add_client(room, &duplicate) == -2);
    assert(room_add_client(room, &bob) == 0);
    room_remove_client(room, &alice);
    room_remove_client(room, &alice);
    assert(strcmp(presence_log, "+alice@lobby;+bob@lobby;-alice@lobby;") == 0);

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

static const chat_room_t *test_client_room(const struct client *client) {
    return client->room;
}

static char state_dir[] = "/tmp/tnt-room-test.XXXXXX";

static void write_state_file(const char *name, const char *text) {
    char path[PATH_MAX];
    FILE *fp;

    snprintf(path, sizeof(path), "%s/%s", state_dir, name);
    fp = fopen(path, "w");
    assert(fp != NULL);
    fputs(text, fp);
    fclose(fp);
}

static void remove_state_file(const char *name) {
    char path[PATH_MAX];

    snprintf(path, sizeof(path), "%s/%s", state_dir, name);
    unlink(path);
}

TEST(room_names_are_validated) {
    assert(tnt_room_name_is_valid("lobby"));
    assert(tnt_room_name_is_valid("ops-2"));
    assert(tnt_room_name_is_valid("abcdefghijklmnopqrst"));
    assert(!tnt_room_name_is_valid("abcdefghijklmnopqrstu"));
    assert(!tnt_room_name_is_valid(""));
    assert(!tnt_room_name_is_valid(NULL));
    assert(!tnt_room_name_is_valid("Ops"));
    assert(!tnt_room_name_is_valid("-ops"));
    assert(!tnt_room_name_is_valid("ops-"));
    assert(!tnt_room_name_is_valid("a_b"));
    assert(!tnt_room_name_is_valid("../x"));
}

TEST(room_registry_reloads_logs_and_creates_rooms) {
    char names[TNT_MAX_ROOMS][TNT_ROOM_NAME_MAX + 1];
    message_t msg;

    assert(mkdtemp(state_dir) != NULL);
    setenv("TNT_STATE_DIR", state_dir, 1);
    write_state_file("messages.log",
                     "#tnt-message-log v3\n2026-01-01T00:00:00Z|alice|lobby line|\n");
    write_state_file("room-ops.log",
                     "#tnt-message-log v3\n2026-01-01T00:00:00Z|bob|ops line|\n"
                     "2026-01-01T00:00:01Z|bob|second|\n");
    write_state_file("room-Bad.log", "");
    write_state_file("room-.log", "");

    chat_room_t *lobby = room_create();
    assert(room_registry_init(lobby) == 0);
    assert(room_get_message_count(lobby) == 1);

    chat_room_t *ops = room_find("ops", false);
    assert(ops != NULL);
    assert(room_get_message_count(ops) == 2);
    assert(room_get_message(ops, 0, &msg));
    assert(strcmp(msg.content, "ops line") == 0);
    assert(strcmp(msg.room, "ops") == 0);

    assert(room_find("lobby", false) == lobby);
    assert(room_find("Bad", true) == NULL);
    assert(room_find("dev", false) == NULL);
    chat_room_t *dev = room_find("dev", true);
    assert(dev != NULL && dev != ops);
    assert(room_find("dev", true) == dev);
    assert(room_list(names, TNT_MAX_ROOMS) == 3);
    assert(strcmp(names[0], "lobby") == 0);
    assert(strcmp(names[1], "dev") == 0);
    assert(strcmp(names[2], "ops") == 0);

    msg = make_msg("carol", "in dev");
    room_tag_message(dev, &msg);
    assert(message_save(&msg) == 0);
    room_broadcast(dev, &msg);
    assert(room_get_message_count(dev) == 1);
    assert(room_get_message_count(lobby) == 1);

    for (int i = 3; i < TNT_MAX_ROOMS; i++) {
        char name[16];
        snprintf(name, sizeof(name), "r%d", i);
        assert(room_find(name, true) != NULL);
    }
    assert(room_find("one-too-many", true) == NULL);

    room_registry_shutdown();
    room_destroy(lobby);

    lobby = room_create();
    assert(room_registry_init(lobby) == 0);
    dev = room_find("dev", false);
    assert(dev != NULL);
    assert(room_get_message_count(dev) == 1);
    assert(room_get_message(dev, 0, &msg));
    assert(strcmp(msg.content, "in dev") == 0);
    assert(room_find("r3", false) == NULL);
    room_registry_shutdown();
    room_destroy(lobby);

    remove_state_file("messages.log");
    remove_state_file("room-ops.log");
    remove_state_file("room-dev.log");
    remove_state_file("room-Bad.log");
    remove_state_file("room-.log");
    rmdir(state_dir);
    unsetenv("TNT_STATE_DIR");
}

TEST(room_members_are_counted_and_woken_per_room) {
    chat_room_t *lobby = room_create();
    client_t alice = {0};
    client_t bob = {0};

    assert(room_registry_init(lobby) == 0);
    chat_room_t *ops = room_find("ops", true);
    assert(ops != NULL);
    snprintf(alice.username, sizeof(alice.username), "alice");
    snprintf(bob.username, sizeof(bob.username), "bob");
    room_set_client_notifier(lobby, count_client_wake);
    room_set_client_room_accessor(lobby, test_client_room);
    assert(room_add_client(lobby, &alice) == 0);
    assert(room_add_client(lobby, &bob) == 0);
    bob.room = ops;

    assert(room_get_client_count(lobby) == 2);
    assert(room_get_client_count(ops) == 1);

    message_t msg = make_msg("bob", "ops only");
    room_broadcast(ops, &msg);
    assert(alice.wake_count == 0);
    assert(bob.wake_count == 1);
    assert(room_get_message_count(lobby) == 0);

    msg = make_msg("alice", "lobby only");
    room_broadcast(lobby, &msg);
    assert(alice.wake_count == 1);
    assert(bob.wake_count == 1);

    room_remove_client(lobby, &alice);
    room_remove_client(lobby, &bob);
    room_registry_shutdown();
    room_destroy(lobby);
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
    RUN_TEST(room_get_message_valid);
    RUN_TEST(room_get_message_invalid_index);
    RUN_TEST(room_get_message_null_args);
    RUN_TEST(room_client_count);
    RUN_TEST(room_remove_nonexistent_client);
    RUN_TEST(room_add_client_full);
    RUN_TEST(room_capacity_follows_tnt_max_connections);
    RUN_TEST(room_rejects_duplicate_names_case_insensitively);
    RUN_TEST(room_reports_presence_changes);
    RUN_TEST(room_message_count_threadsafe);
    RUN_TEST(room_names_are_validated);
    RUN_TEST(room_registry_reloads_logs_and_creates_rooms);
    RUN_TEST(room_members_are_counted_and_woken_per_room);

    printf("\nAll %d tests passed!\n", tests_passed);
    return 0;
}
