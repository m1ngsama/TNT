#include "../../include/module_runtime.h"
#include "../../include/chat_room.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    test_##name(); \
    printf("ok\n"); \
    tests_passed++; \
} while (0)

static int tests_passed = 0;
static char module_dir[PATH_MAX];

chat_room_t *g_room = NULL;

void room_broadcast(chat_room_t *room, const message_t *msg) {
    (void)room;
    (void)msg;
}

int message_save(const message_t *msg) {
    (void)msg;
    return 0;
}

void notify_mentions(const char *content, const void *sender) {
    (void)content;
    (void)sender;
}

static void cleanup_module_dir(void) {
    if (module_dir[0] == '\0') return;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/tnt-module.json", module_dir);
    unlink(path);
    rmdir(module_dir);
    module_dir[0] = '\0';
}

static void setup_module_dir(void) {
    const char *tmp = getenv("TMPDIR");

    cleanup_module_dir();
    if (!tmp || tmp[0] == '\0') tmp = "/tmp";
    snprintf(module_dir, sizeof(module_dir), "%s/tnt-module-test.XXXXXX", tmp);
    assert(mkdtemp(module_dir) != NULL);
}

static void write_manifest(const char *body) {
    char path[PATH_MAX];
    FILE *fp;

    snprintf(path, sizeof(path), "%s/tnt-module.json", module_dir);
    fp = fopen(path, "wb");
    assert(fp != NULL);
    fputs(body, fp);
    fclose(fp);
}

TEST(loads_valid_manifest) {
    tnt_module_manifest_t manifest;

    setup_module_dir();
    write_manifest(
        "{"
        "\"protocol\":\"tnt.module.v1\","
        "\"name\":\"echo-module\","
        "\"version\":\"0.1.0\","
        "\"entrypoint\":\"./echo-module.sh\","
        "\"permissions\":[\"message:read\",\"message:create\"],"
        "\"events\":[\"message.created\"]"
        "}");

    assert(tnt_module_manifest_load(module_dir, &manifest) == 0);
    assert(strcmp(manifest.name, "echo-module") == 0);
    assert(strcmp(manifest.entrypoint, "./echo-module.sh") == 0);
    assert(manifest.wants_message_created);
    assert(manifest.can_read_messages);
    assert(manifest.can_create_messages);
    cleanup_module_dir();
}

TEST(rejects_wrong_protocol) {
    tnt_module_manifest_t manifest;

    setup_module_dir();
    write_manifest(
        "{\"protocol\":\"tnt.module.v2\",\"name\":\"echo\","
        "\"entrypoint\":\"./echo.sh\","
        "\"permissions\":[\"message:read\",\"message:create\"],"
        "\"events\":[\"message.created\"]}");
    assert(tnt_module_manifest_load(module_dir, &manifest) < 0);
    cleanup_module_dir();
}

TEST(rejects_missing_permissions_or_events) {
    tnt_module_manifest_t manifest;

    setup_module_dir();
    write_manifest(
        "{\"protocol\":\"tnt.module.v1\",\"name\":\"echo\","
        "\"entrypoint\":\"./echo.sh\","
        "\"permissions\":[\"message:read\"],"
        "\"events\":[\"message.created\"]}");
    assert(tnt_module_manifest_load(module_dir, &manifest) < 0);

    write_manifest(
        "{\"protocol\":\"tnt.module.v1\",\"name\":\"echo\","
        "\"entrypoint\":\"./echo.sh\","
        "\"permissions\":[\"message:read\",\"message:create\"],"
        "\"events\":[]}");
    assert(tnt_module_manifest_load(module_dir, &manifest) < 0);
    cleanup_module_dir();
}

TEST(rejects_unsafe_entrypoint) {
    tnt_module_manifest_t manifest;

    setup_module_dir();
    write_manifest(
        "{\"protocol\":\"tnt.module.v1\",\"name\":\"echo\","
        "\"entrypoint\":\"../echo.sh\","
        "\"permissions\":[\"message:read\",\"message:create\"],"
        "\"events\":[\"message.created\"]}");
    assert(tnt_module_manifest_load(module_dir, &manifest) < 0);

    write_manifest(
        "{\"protocol\":\"tnt.module.v1\",\"name\":\"echo\","
        "\"entrypoint\":\"/tmp/echo.sh\","
        "\"permissions\":[\"message:read\",\"message:create\"],"
        "\"events\":[\"message.created\"]}");
    assert(tnt_module_manifest_load(module_dir, &manifest) < 0);
    cleanup_module_dir();
}

int main(void) {
    printf("Running module runtime unit tests...\n\n");

    RUN_TEST(loads_valid_manifest);
    RUN_TEST(rejects_wrong_protocol);
    RUN_TEST(rejects_missing_permissions_or_events);
    RUN_TEST(rejects_unsafe_entrypoint);

    printf("\nAll %d module runtime tests passed.\n", tests_passed);
    return 0;
}
