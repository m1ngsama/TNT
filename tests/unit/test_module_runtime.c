#include "../../include/module_runtime.h"
#include "../../include/chat_room.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#define MODULE_WRITE_ERROR (-1)
#define MODULE_WRITE_TIMEOUT (-2)
#define MODULE_WRITE_STOPPING (-3)
#define MODULE_READ_ERROR (-1)
#define MODULE_READ_STOPPING (-2)

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

static double monotonic_seconds(void) {
    struct timespec now;

    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static void sleep_millis(int millis) {
    struct timespec delay = {
        .tv_sec = millis / 1000,
        .tv_nsec = (long)(millis % 1000) * 1000000L,
    };

    while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {
    }
}

static void fill_pipe(int fd) {
    char data[4096];
    int flags = fcntl(fd, F_GETFL);

    assert(flags >= 0);
    assert(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
    memset(data, 'x', sizeof(data));
    for (;;) {
        ssize_t n = write(fd, data, sizeof(data));
        if (n > 0) continue;
        assert(n < 0);
        if (errno == EINTR) continue;
        assert(errno == EAGAIN || errno == EWOULDBLOCK);
        return;
    }
}

typedef struct {
    int fd;
    int result;
} blocked_writer_args_t;

typedef struct {
    int fd;
    int result;
    char line[64];
} blocked_reader_args_t;

typedef struct {
    int fd;
    int writes;
    int delay_ms;
} drip_writer_args_t;

static void *run_blocked_writer(void *arg) {
    blocked_writer_args_t *writer = arg;

    writer->result = tnt_module_runtime_test_write_fd(
        writer->fd, "x", 1, 5000, true);
    return NULL;
}

static void *run_blocked_reader(void *arg) {
    blocked_reader_args_t *reader = arg;

    reader->result = tnt_module_runtime_test_read_fd(
        reader->fd, reader->line, sizeof(reader->line), 5000, true);
    return NULL;
}

static void *run_drip_writer(void *arg) {
    drip_writer_args_t *writer = arg;

    for (int i = 0; i < writer->writes; i++) {
        ssize_t n;

        sleep_millis(writer->delay_ms);
        do {
            n = write(writer->fd, "x", 1);
        } while (n < 0 && errno == EINTR);
        assert(n == 1);
    }
    return NULL;
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

TEST(rejects_invalid_module_names) {
    tnt_module_manifest_t manifest;
    char long_name[TNT_MODULE_NAME_MAX + 2];
    char body[512];

    setup_module_dir();
    write_manifest(
        "{\"protocol\":\"tnt.module.v1\",\"name\":\"Echo\","
        "\"entrypoint\":\"./echo.sh\","
        "\"permissions\":[\"message:read\",\"message:create\"],"
        "\"events\":[\"message.created\"]}");
    assert(tnt_module_manifest_load(module_dir, &manifest) < 0);

    write_manifest(
        "{\"protocol\":\"tnt.module.v1\",\"name\":\"echo_module\","
        "\"entrypoint\":\"./echo.sh\","
        "\"permissions\":[\"message:read\",\"message:create\"],"
        "\"events\":[\"message.created\"]}");
    assert(tnt_module_manifest_load(module_dir, &manifest) < 0);

    write_manifest(
        "{\"protocol\":\"tnt.module.v1\",\"name\":\"-echo\","
        "\"entrypoint\":\"./echo.sh\","
        "\"permissions\":[\"message:read\",\"message:create\"],"
        "\"events\":[\"message.created\"]}");
    assert(tnt_module_manifest_load(module_dir, &manifest) < 0);

    write_manifest(
        "{\"protocol\":\"tnt.module.v1\",\"name\":\"echo-\","
        "\"entrypoint\":\"./echo.sh\","
        "\"permissions\":[\"message:read\",\"message:create\"],"
        "\"events\":[\"message.created\"]}");
    assert(tnt_module_manifest_load(module_dir, &manifest) < 0);

    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    snprintf(body, sizeof(body),
             "{\"protocol\":\"tnt.module.v1\",\"name\":\"%s\","
             "\"entrypoint\":\"./echo.sh\","
             "\"permissions\":[\"message:read\",\"message:create\"],"
             "\"events\":[\"message.created\"]}",
             long_name);
    write_manifest(body);
    assert(tnt_module_manifest_load(module_dir, &manifest) < 0);

    cleanup_module_dir();
}

TEST(module_stdin_write_has_absolute_timeout) {
    int fds[2];
    double started;
    double elapsed;
    int result;

    assert(pipe(fds) == 0);
    fill_pipe(fds[1]);

    started = monotonic_seconds();
    result = tnt_module_runtime_test_write_fd(
        fds[1], "x", 1, 80, false);
    elapsed = monotonic_seconds() - started;

    assert(result == MODULE_WRITE_TIMEOUT);
    assert(elapsed >= 0.04);
    assert(elapsed < 2.0);
    close(fds[0]);
    close(fds[1]);
}

TEST(module_stdin_write_reports_closed_reader) {
    int fds[2];
    double started;
    double elapsed;
    int result;

    assert(signal(SIGPIPE, SIG_IGN) != SIG_ERR);
    assert(pipe(fds) == 0);
    close(fds[0]);

    started = monotonic_seconds();
    result = tnt_module_runtime_test_write_fd(
        fds[1], "event\n", strlen("event\n"), 1000, false);
    elapsed = monotonic_seconds() - started;

    assert(result == MODULE_WRITE_ERROR);
    assert(elapsed < 1.0);
    close(fds[1]);
}

TEST(shutdown_cancels_backpressured_module_write) {
    int fds[2];
    pthread_t thread;
    blocked_writer_args_t writer;
    double started;
    double elapsed;

    assert(pipe(fds) == 0);
    fill_pipe(fds[1]);
    tnt_module_runtime_test_reset_stop();

    writer.fd = fds[1];
    writer.result = 0;
    assert(pthread_create(&thread, NULL, run_blocked_writer, &writer) == 0);
    sleep_millis(50);

    started = monotonic_seconds();
    tnt_module_runtime_shutdown();
    assert(pthread_join(thread, NULL) == 0);
    elapsed = monotonic_seconds() - started;

    assert(writer.result == MODULE_WRITE_STOPPING);
    assert(elapsed < 1.0);
    close(fds[0]);
    close(fds[1]);
}

TEST(module_stdout_drip_feed_cannot_extend_deadline) {
    int fds[2];
    pthread_t thread;
    drip_writer_args_t writer;
    char line[64];
    double started;
    double elapsed;
    int result;

    assert(pipe(fds) == 0);
    writer.fd = fds[1];
    /* Keep producing bytes far beyond the asserted deadline.  An
     * implementation that restarts its 80 ms budget per byte takes over one
     * second here and fails the bound below deterministically. */
    writer.writes = 50;
    writer.delay_ms = 20;
    /* Seed one partial-frame byte synchronously.  The assertion is about an
     * absolute read deadline, not whether the scheduler starts the helper
     * thread before an 80 ms timer expires on a busy macOS runner. */
    assert(write(fds[1], "x", 1) == 1);
    assert(pthread_create(&thread, NULL, run_drip_writer, &writer) == 0);

    started = monotonic_seconds();
    result = tnt_module_runtime_test_read_fd(
        fds[0], line, sizeof(line), 80, false);
    elapsed = monotonic_seconds() - started;

    assert(result == MODULE_READ_ERROR);
    assert(elapsed >= 0.04);
    assert(elapsed < 0.5);
    assert(pthread_join(thread, NULL) == 0);
    close(fds[0]);
    close(fds[1]);
}

TEST(shutdown_cancels_waiting_module_read) {
    int fds[2];
    pthread_t thread;
    blocked_reader_args_t reader;
    double started;
    double elapsed;

    assert(pipe(fds) == 0);
    tnt_module_runtime_test_reset_stop();
    reader.fd = fds[0];
    reader.result = 0;
    reader.line[0] = '\0';
    assert(pthread_create(&thread, NULL, run_blocked_reader, &reader) == 0);
    sleep_millis(50);

    started = monotonic_seconds();
    tnt_module_runtime_shutdown();
    assert(pthread_join(thread, NULL) == 0);
    elapsed = monotonic_seconds() - started;

    assert(reader.result == MODULE_READ_STOPPING);
    assert(elapsed < 1.0);
    close(fds[0]);
    close(fds[1]);
}

int main(void) {
    printf("Running module runtime unit tests...\n\n");

    RUN_TEST(loads_valid_manifest);
    RUN_TEST(rejects_wrong_protocol);
    RUN_TEST(rejects_missing_permissions_or_events);
    RUN_TEST(rejects_unsafe_entrypoint);
    RUN_TEST(rejects_invalid_module_names);
    RUN_TEST(module_stdin_write_has_absolute_timeout);
    RUN_TEST(module_stdin_write_reports_closed_reader);
    RUN_TEST(shutdown_cancels_backpressured_module_write);
    RUN_TEST(module_stdout_drip_feed_cannot_extend_deadline);
    RUN_TEST(shutdown_cancels_waiting_module_read);

    printf("\nAll %d module runtime tests passed.\n", tests_passed);
    return 0;
}
