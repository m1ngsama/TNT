#include "module_runtime.h"

#include "chat_room.h"
#include "common.h"
#include "config_defaults.h"
#include "json_text.h"
#include "module_protocol.h"
#include "utf8.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TNT_MODULE_LINE_MAX 4096
#define TNT_MODULE_READ_BUFFER_SIZE 1024
#define TNT_MODULE_HANDSHAKE_TIMEOUT_MS 2000
#define TNT_MODULE_WRITE_TIMEOUT_MS 250
#define TNT_MODULE_IO_POLL_SLICE_MS 25
#define TNT_MODULE_MAX_RESPONSES_PER_EVENT 8
#define TNT_MODULE_MAX_INVALID_RESPONSES 3
#define TNT_MODULE_STOP_GRACE_MS 500
#define TNT_MODULE_MAX_OPEN_FILES 64
#define TNT_MODULE_WORKER_STACK_SIZE (256 * 1024)

struct client;
void notify_mentions(const char *content, const struct client *sender);

typedef struct module_event {
    message_t msg;
    uint64_t event_id;
} module_event_t;

typedef struct module_read_buffer {
    char data[TNT_MODULE_READ_BUFFER_SIZE];
    size_t begin;
    size_t end;
} module_read_buffer_t;

typedef struct module_process {
    tnt_module_manifest_t manifest;
    pid_t pid;
    pid_t pgid;
    int stdin_fd;
    int stdout_fd;
    module_read_buffer_t output;
    int invalid_responses;
    atomic_bool active;
    pthread_t worker;
    pthread_mutex_t queue_lock;
    pthread_cond_t queue_cond;
    module_event_t *event_queue;
    size_t queue_read_index;
    size_t queued_event_count;
    bool queue_running;
    bool saturation_reported;
    bool queue_initialized;
    bool worker_started;
} module_process_t;

typedef enum module_response_action {
    MODULE_RESPONSE_CONTINUE,
    MODULE_RESPONSE_DONE,
    MODULE_RESPONSE_INVALID
} module_response_action_t;

typedef enum module_write_result {
    MODULE_WRITE_OK = 0,
    MODULE_WRITE_ERROR = -1,
    MODULE_WRITE_TIMEOUT = -2,
    MODULE_WRITE_STOPPING = -3
} module_write_result_t;

typedef enum module_read_result {
    MODULE_READ_ERROR = -1,
    MODULE_READ_STOPPING = -2
} module_read_result_t;

typedef enum module_queue_push_result {
    MODULE_QUEUE_PUSHED,
    MODULE_QUEUE_DROPPED,
    MODULE_QUEUE_SATURATED
} module_queue_push_result_t;

static module_process_t g_modules[TNT_MAX_MODULES];
static int g_module_count = 0;
static bool g_accepting = false;
static uint64_t g_next_event_id = 0;
static atomic_bool g_stop_requested = ATOMIC_VAR_INIT(true);
static pthread_mutex_t g_dispatch_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_module_publish_lock = PTHREAD_MUTEX_INITIALIZER;

/* Read once at init: an event dispatch must not pay for getenv. */
static int g_module_response_timeout_ms =
    TNT_DEFAULT_MODULE_RESPONSE_TIMEOUT_MS;
static pthread_mutex_t g_lifecycle_lock = PTHREAD_MUTEX_INITIALIZER;

static bool is_safe_relative_entrypoint(const char *entrypoint) {
    if (!entrypoint || entrypoint[0] == '\0' || entrypoint[0] == '/') {
        return false;
    }
    if (strstr(entrypoint, "..") != NULL) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)entrypoint; *p; p++) {
        if (*p <= 32 || *p == 127 || *p == '|' || *p == ';' ||
            *p == '&' || *p == '`' || *p == '$' || *p == '<' ||
            *p == '>' || *p == '\\') {
            return false;
        }
    }
    return true;
}

static bool is_valid_module_name(const char *name) {
    size_t len;

    if (!name || name[0] == '\0') {
        return false;
    }

    len = strlen(name);
    if (len > TNT_MODULE_NAME_MAX ||
        len >= sizeof(((tnt_module_manifest_t *)0)->name) ||
        name[0] == '-' || name[len - 1] == '-') {
        return false;
    }

    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
            *p == '-') {
            continue;
        }
        return false;
    }

    return true;
}

static bool json_array_contains_string(const char *json, const char *key,
                                       const char *value) {
    char needle[128];
    const char *p;

    if (!json || !key || !value ||
        snprintf(needle, sizeof(needle), "\"%s\"", key) >=
            (int)sizeof(needle)) {
        return false;
    }

    p = strstr(json, needle);
    if (!p) return false;
    p = strchr(p + strlen(needle), ':');
    if (!p) return false;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '[') return false;
    p++;

    while (*p) {
        char item[128];
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' ||
               *p == ',') {
            p++;
        }
        if (*p == ']') return false;
        if (*p != '"') return false;
        const char *cursor = p;
        size_t pos = 0;
        item[0] = '\0';
        cursor++;
        while (*cursor && *cursor != '"') {
            if (*cursor == '\\') {
                cursor++;
                if (!*cursor) return false;
            }
            if (pos + 1 >= sizeof(item)) return false;
            item[pos++] = *cursor++;
        }
        if (*cursor != '"') return false;
        item[pos] = '\0';
        if (strcmp(item, value) == 0) return true;
        p = cursor + 1;
    }

    return false;
}

int tnt_module_manifest_load(const char *module_dir,
                             tnt_module_manifest_t *out) {
    char manifest_path[PATH_MAX];
    char manifest[TNT_MODULE_LINE_MAX];
    char protocol[64];
    FILE *fp;
    size_t n;

    if (!module_dir || module_dir[0] == '\0' || !out) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    if (snprintf(manifest_path, sizeof(manifest_path), "%s/tnt-module.json",
                 module_dir) >= (int)sizeof(manifest_path)) {
        return -1;
    }

    fp = fopen(manifest_path, "rb");
    if (!fp) {
        return -1;
    }
    n = fread(manifest, 1, sizeof(manifest) - 1, fp);
    fclose(fp);
    manifest[n] = '\0';

    if (n == 0 || n >= sizeof(manifest) - 1 ||
        !tnt_json_get_string_field(manifest, "protocol", protocol,
                                   sizeof(protocol)) ||
        strcmp(protocol, TNT_MODULE_PROTOCOL_VERSION) != 0 ||
        !tnt_json_get_string_field(manifest, "name", out->name,
                                   sizeof(out->name)) ||
        !tnt_json_get_string_field(manifest, "entrypoint", out->entrypoint,
                                   sizeof(out->entrypoint)) ||
        !is_valid_module_name(out->name) ||
        !is_safe_relative_entrypoint(out->entrypoint)) {
        return -1;
    }

    out->wants_message_created = json_array_contains_string(
        manifest, "events", TNT_MODULE_EVENT_MESSAGE_CREATED);
    out->can_read_messages = json_array_contains_string(
        manifest, "permissions", "message:read");
    out->can_create_messages = json_array_contains_string(
        manifest, "permissions", "message:create");

    if (!out->wants_message_created || !out->can_read_messages ||
        !out->can_create_messages) {
        return -1;
    }

    return 0;
}

static int64_t monotonic_millis(void) {
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
        return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    }
    return (int64_t)time(NULL) * 1000;
}

static int set_fd_nonblocking(int fd) {
    int flags;

    if (fd < 0) return -1;
    flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }
    return 0;
}

/* Write one complete protocol record without ever blocking the module worker
 * indefinitely.  Module stdin is O_NONBLOCK; poll supplies backpressure up to
 * an absolute deadline.  Short poll slices let shutdown cancel a full pipe
 * promptly instead of waiting for the entire normal write budget. */
static module_write_result_t write_module_input(int fd, const char *data,
                                                size_t len, int timeout_ms,
                                                bool cancel_on_stop) {
    size_t written = 0;
    int64_t deadline;

    if (fd < 0 || (!data && len > 0) || timeout_ms < 0) {
        return MODULE_WRITE_ERROR;
    }
    if (len == 0) return MODULE_WRITE_OK;

    deadline = monotonic_millis() + timeout_ms;
    while (written < len) {
        ssize_t n;

        if (cancel_on_stop &&
            atomic_load_explicit(&g_stop_requested, memory_order_acquire)) {
            return MODULE_WRITE_STOPPING;
        }
        if (monotonic_millis() >= deadline) {
            return MODULE_WRITE_TIMEOUT;
        }

        n = write(fd, data + written, len - written);
        if (n > 0) {
            written += (size_t)n;
            continue;
        }
        if (n == 0) {
            return MODULE_WRITE_ERROR;
        }
        if (errno == EINTR) {
            if (monotonic_millis() >= deadline) {
                return MODULE_WRITE_TIMEOUT;
            }
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return MODULE_WRITE_ERROR;
        }

        for (;;) {
            struct pollfd wait_fd = {
                .fd = fd,
                .events = POLLOUT,
                .revents = 0,
            };
            int64_t now = monotonic_millis();
            int64_t remaining = deadline - now;
            int wait_ms;
            int ready;

            if (cancel_on_stop &&
                atomic_load_explicit(&g_stop_requested,
                                     memory_order_acquire)) {
                return MODULE_WRITE_STOPPING;
            }
            if (remaining <= 0) {
                return MODULE_WRITE_TIMEOUT;
            }

            wait_ms = remaining > INT_MAX ? INT_MAX : (int)remaining;
            if (cancel_on_stop && wait_ms > TNT_MODULE_IO_POLL_SLICE_MS) {
                wait_ms = TNT_MODULE_IO_POLL_SLICE_MS;
            }
            ready = poll(&wait_fd, 1, wait_ms);
            if (ready > 0) {
                if (wait_fd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    return MODULE_WRITE_ERROR;
                }
                if (wait_fd.revents & POLLOUT) {
                    break;
                }
                continue;
            }
            if (ready == 0) {
                continue;
            }
            if (errno != EINTR) {
                return MODULE_WRITE_ERROR;
            }
        }
    }

    return MODULE_WRITE_OK;
}

static int read_line_deadline(int fd, module_read_buffer_t *buffer,
                              char *line, size_t line_size, int64_t deadline,
                              bool cancel_on_stop) {
    size_t pos = 0;

    if (fd < 0 || !buffer || !line || line_size < 2) {
        return MODULE_READ_ERROR;
    }
    line[0] = '\0';

    for (;;) {
        if (cancel_on_stop &&
            atomic_load_explicit(&g_stop_requested,
                                 memory_order_acquire)) {
            return MODULE_READ_STOPPING;
        }
        if (monotonic_millis() >= deadline) {
            return pos == 0 ? 0 : MODULE_READ_ERROR;
        }

        while (buffer->begin < buffer->end) {
            unsigned char c;

            c = (unsigned char)buffer->data[buffer->begin++];
            if (c == '\n') {
                line[pos] = '\0';
                return (int)pos;
            }
            if (c < 32 && c != '\t' && c != '\r') {
                return MODULE_READ_ERROR;
            }
            line[pos++] = (char)c;
            if (pos + 1 >= line_size) {
                line[pos] = '\0';
                return MODULE_READ_ERROR;
            }
        }

        buffer->begin = 0;
        buffer->end = 0;

        for (;;) {
            struct pollfd wait_fd = {
                .fd = fd,
                .events = POLLIN,
                .revents = 0,
            };
            int64_t remaining = deadline - monotonic_millis();
            int wait_ms;
            int ready;
            ssize_t n;

            if (cancel_on_stop &&
                atomic_load_explicit(&g_stop_requested,
                                     memory_order_acquire)) {
                return MODULE_READ_STOPPING;
            }
            if (remaining <= 0) {
                return pos == 0 ? 0 : MODULE_READ_ERROR;
            }

            wait_ms = remaining > INT_MAX ? INT_MAX : (int)remaining;
            if (cancel_on_stop && wait_ms > TNT_MODULE_IO_POLL_SLICE_MS) {
                wait_ms = TNT_MODULE_IO_POLL_SLICE_MS;
            }
            ready = poll(&wait_fd, 1, wait_ms);
            if (ready == 0) {
                continue;
            }
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return MODULE_READ_ERROR;
            }
            if (!(wait_fd.revents & POLLIN)) {
                if (wait_fd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    return MODULE_READ_ERROR;
                }
                continue;
            }

            n = read(fd, buffer->data, sizeof(buffer->data));
            if (n > 0) {
                buffer->end = (size_t)n;
                break;
            }
            if (n == 0) {
                return MODULE_READ_ERROR;
            }
            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                return MODULE_READ_ERROR;
            }
        }
    }
}

static void set_module_rlimit(int resource, rlim_t value) {
    struct rlimit limit;

    limit.rlim_cur = value;
    limit.rlim_max = value;
    (void)setrlimit(resource, &limit);
}

static void close_inherited_fds(void) {
    long open_max = sysconf(_SC_OPEN_MAX);

    if (open_max < 0 || open_max > 65536) {
        open_max = 1024;
    }

    for (int fd = 3; fd < open_max; fd++) {
        close(fd);
    }
}

static void prepare_module_child(void) {
    close_inherited_fds();

#ifdef RLIMIT_CORE
    set_module_rlimit(RLIMIT_CORE, 0);
#endif
#ifdef RLIMIT_NOFILE
    set_module_rlimit(RLIMIT_NOFILE, TNT_MODULE_MAX_OPEN_FILES);
#endif
    unsetenv("TNT_ACCESS_TOKEN");
    unsetenv("LD_PRELOAD");
    unsetenv("LD_LIBRARY_PATH");
#ifdef __APPLE__
    unsetenv("DYLD_INSERT_LIBRARIES");
    unsetenv("DYLD_LIBRARY_PATH");
#endif
}

static void sleep_millis(int millis) {
    struct timespec ts;

    ts.tv_sec = millis / 1000;
    ts.tv_nsec = (long)(millis % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {
    }
}

static bool reap_module_child_nonblocking(pid_t pid) {
    int status;
    pid_t result;

    if (pid <= 0) {
        return true;
    }

    do {
        result = waitpid(pid, &status, WNOHANG);
    } while (result < 0 && errno == EINTR);

    return result == pid || (result < 0 && errno == ECHILD);
}

static void reap_module_child(pid_t pid) {
    int status;

    if (pid <= 0) {
        return;
    }

    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            break;
        }
    }
}

static bool module_process_group_alive(pid_t pgid) {
    if (pgid <= 0) {
        return false;
    }

    if (killpg(pgid, 0) == 0) {
        return true;
    }
    return errno == EPERM;
}

static void terminate_module_process_group(pid_t pid, pid_t pgid) {
    bool child_reaped = false;
    int waited_ms = 0;

    if (pid <= 0) {
        return;
    }

    if (pgid <= 0 || killpg(pgid, SIGTERM) < 0) {
        (void)kill(pid, SIGTERM);
    }

    while (waited_ms < TNT_MODULE_STOP_GRACE_MS) {
        if (!child_reaped) {
            child_reaped = reap_module_child_nonblocking(pid);
        }
        if (child_reaped && !module_process_group_alive(pgid)) {
            return;
        }
        sleep_millis(10);
        waited_ms += 10;
    }

    if (pgid > 0) {
        (void)killpg(pgid, SIGKILL);
    }
    if (!child_reaped) {
        (void)kill(pid, SIGKILL);
        reap_module_child(pid);
    }
}

static void close_module_process(module_process_t *module) {
    if (!module ||
        !atomic_exchange_explicit(&module->active, false,
                                  memory_order_acq_rel)) {
        return;
    }

    if (module->stdin_fd >= 0) {
        close(module->stdin_fd);
        module->stdin_fd = -1;
    }
    if (module->stdout_fd >= 0) {
        close(module->stdout_fd);
        module->stdout_fd = -1;
    }
    if (module->pid > 0) {
        terminate_module_process_group(module->pid, module->pgid);
        module->pid = -1;
        module->pgid = -1;
    }
}

static bool handshake_ok(const char *line) {
    char type[64];
    char protocol[64];

    return tnt_json_get_string_field(line, "type", type, sizeof(type)) &&
           strcmp(type, "handshake.ok") == 0 &&
           tnt_json_get_string_field(line, "protocol", protocol,
                                     sizeof(protocol)) &&
           strcmp(protocol, TNT_MODULE_PROTOCOL_VERSION) == 0;
}

static bool establish_child_process_group(pid_t pid) {
    return setpgid(pid, pid) == 0 || getpgid(pid) == pid;
}

static int start_module_process(const char *module_dir,
                                module_process_t *module) {
    int in_pipe[2] = {-1, -1};
    int out_pipe[2] = {-1, -1};
    char handshake[512] = "";
    char line[TNT_MODULE_LINE_MAX];
    size_t pos = 0;

    if (!module) {
        return -1;
    }
    if (pipe(in_pipe) < 0) {
        return -1;
    }
    if (pipe(out_pipe) < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        if (setpgid(0, 0) < 0) {
            _exit(127);
        }
        close(in_pipe[1]);
        close(out_pipe[0]);
        if (dup2(in_pipe[0], STDIN_FILENO) < 0 ||
            dup2(out_pipe[1], STDOUT_FILENO) < 0 ||
            chdir(module_dir) < 0) {
            _exit(127);
        }
        close(in_pipe[0]);
        close(out_pipe[1]);
        prepare_module_child();
        execl(module->manifest.entrypoint, module->manifest.entrypoint,
              (char *)NULL);
        _exit(127);
    }

    if (!establish_child_process_group(pid)) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        (void)kill(pid, SIGKILL);
        reap_module_child(pid);
        return -1;
    }

    close(in_pipe[0]);
    close(out_pipe[1]);

    module->pid = pid;
    module->pgid = pid;
    module->stdin_fd = in_pipe[1];
    module->stdout_fd = out_pipe[0];
    atomic_store_explicit(&module->active, true, memory_order_release);

    if (tnt_module_append_handshake(handshake, sizeof(handshake), &pos,
                                    TNT_VERSION) < 0 ||
        set_fd_nonblocking(module->stdin_fd) < 0 ||
        set_fd_nonblocking(module->stdout_fd) < 0 ||
        write_module_input(module->stdin_fd, handshake, strlen(handshake),
                           TNT_MODULE_HANDSHAKE_TIMEOUT_MS, false) !=
            MODULE_WRITE_OK ||
        read_line_deadline(module->stdout_fd, &module->output, line,
                           sizeof(line),
                           monotonic_millis() +
                               TNT_MODULE_HANDSHAKE_TIMEOUT_MS,
                           false) <= 0 ||
        !handshake_ok(line)) {
        close_module_process(module);
        return -1;
    }

    return 0;
}

static int module_queue_init(module_process_t *module) {
    if (!module) {
        return -1;
    }

    module->event_queue = calloc(TNT_MODULE_QUEUE_LIMIT,
                                 sizeof(*module->event_queue));
    if (!module->event_queue) {
        return -1;
    }
    if (pthread_mutex_init(&module->queue_lock, NULL) != 0) {
        free(module->event_queue);
        module->event_queue = NULL;
        return -1;
    }
    if (pthread_cond_init(&module->queue_cond, NULL) != 0) {
        pthread_mutex_destroy(&module->queue_lock);
        free(module->event_queue);
        module->event_queue = NULL;
        return -1;
    }

    module->queue_read_index = 0;
    module->queued_event_count = 0;
    module->queue_running = true;
    module->saturation_reported = false;
    module->queue_initialized = true;
    return 0;
}

static void module_queue_stop(module_process_t *module) {
    if (!module || !module->queue_initialized) return;

    pthread_mutex_lock(&module->queue_lock);
    module->queue_running = false;
    pthread_cond_broadcast(&module->queue_cond);
    pthread_mutex_unlock(&module->queue_lock);
}

static void module_queue_destroy(module_process_t *module) {
    if (!module || !module->queue_initialized) return;

    free(module->event_queue);
    module->event_queue = NULL;
    module->queue_read_index = 0;
    module->queued_event_count = 0;
    module->saturation_reported = false;
    pthread_cond_destroy(&module->queue_cond);
    pthread_mutex_destroy(&module->queue_lock);
    module->queue_initialized = false;
}

static module_queue_push_result_t module_queue_push(module_process_t *module,
                                                    const message_t *msg,
                                                    uint64_t event_id) {
    size_t queue_write_index;
    bool active;
    bool queue_full;

    if (!module || !msg || !module->queue_initialized ||
        !atomic_load_explicit(&module->active, memory_order_acquire)) {
        return MODULE_QUEUE_DROPPED;
    }

    pthread_mutex_lock(&module->queue_lock);
    queue_full = module->queued_event_count >= TNT_MODULE_QUEUE_LIMIT;
    active = atomic_load_explicit(&module->active, memory_order_acquire);
    if (!module->queue_running || queue_full || !active) {
        if (module->queue_running && queue_full && active &&
            !module->saturation_reported) {
            module->saturation_reported = true;
            pthread_mutex_unlock(&module->queue_lock);
            return MODULE_QUEUE_SATURATED;
        }
        pthread_mutex_unlock(&module->queue_lock);
        return MODULE_QUEUE_DROPPED;
    }

    queue_write_index =
        (module->queue_read_index + module->queued_event_count) %
        TNT_MODULE_QUEUE_LIMIT;
    module->event_queue[queue_write_index].msg = *msg;
    module->event_queue[queue_write_index].event_id = event_id;
    module->queued_event_count++;
    pthread_cond_signal(&module->queue_cond);
    pthread_mutex_unlock(&module->queue_lock);
    return MODULE_QUEUE_PUSHED;
}

static bool module_queue_pop(module_process_t *module, module_event_t *event) {
    if (!module || !event || !module->queue_initialized) return false;

    pthread_mutex_lock(&module->queue_lock);
    while (module->queue_running && module->queued_event_count == 0) {
        pthread_cond_wait(&module->queue_cond, &module->queue_lock);
    }
    if (!module->queue_running) {
        pthread_mutex_unlock(&module->queue_lock);
        return false;
    }

    *event = module->event_queue[module->queue_read_index];
    module->queue_read_index =
        (module->queue_read_index + 1) % TNT_MODULE_QUEUE_LIMIT;
    module->queued_event_count--;
    if (module->queued_event_count <= TNT_MODULE_QUEUE_LIMIT / 2) {
        module->saturation_reported = false;
    }
    pthread_mutex_unlock(&module->queue_lock);
    return true;
}

static void publish_module_message(const module_process_t *module,
                                   const char *plain_text) {
    message_t msg = {
        .timestamp = time(NULL),
    };

    if (!module || !plain_text || plain_text[0] == '\0') return;

    snprintf(msg.username, sizeof(msg.username), "module:%.*s",
             TNT_MODULE_NAME_MAX, module->manifest.name);
    snprintf(msg.content, sizeof(msg.content), "%s", plain_text);

    pthread_mutex_lock(&g_module_publish_lock);
    if (message_save(&msg) < 0) {
        pthread_mutex_unlock(&g_module_publish_lock);
        fprintf(stderr, "module runtime: failed to persist module message\n");
        return;
    }

    room_broadcast(g_room, &msg);
    notify_mentions(msg.content, NULL);
    pthread_mutex_unlock(&g_module_publish_lock);
}

static module_response_action_t handle_module_response(module_process_t *module,
                                                       const char *line) {
    tnt_module_message_create_t create;
    char type[64];

    if (!module || !line || line[0] == '\0') {
        return MODULE_RESPONSE_INVALID;
    }

    if (tnt_module_parse_message_create(line, &create)) {
        publish_module_message(module, create.plain_text);
        return MODULE_RESPONSE_CONTINUE;
    }
    if (tnt_json_get_string_field(line, "type", type, sizeof(type)) &&
        strcmp(type, "event.ok") == 0) {
        return MODULE_RESPONSE_DONE;
    }

    fprintf(stderr, "module runtime: ignored invalid response from %s\n",
            module->manifest.name);
    return MODULE_RESPONSE_INVALID;
}

static void deliver_message_to_module(module_process_t *module,
                                      const message_t *msg,
                                      uint64_t event_id) {
    char event[TNT_MODULE_LINE_MAX] = "";
    char line[TNT_MODULE_LINE_MAX];
    char message_id[64];
    size_t pos = 0;
    int responses = 0;
    int64_t response_deadline;
    module_write_result_t write_result;

    if (!module ||
        !atomic_load_explicit(&module->active, memory_order_acquire) || !msg ||
        atomic_load_explicit(&g_stop_requested, memory_order_acquire)) {
        return;
    }

    snprintf(message_id, sizeof(message_id), "local-%llu",
             (unsigned long long)event_id);
    if (tnt_module_append_message_created(event, sizeof(event), &pos,
                                          message_id, msg) < 0) {
        fprintf(stderr,
                "module runtime: disabling %s after event encoding failure\n",
                module->manifest.name);
        close_module_process(module);
        return;
    }

    write_result = write_module_input(
        module->stdin_fd, event, strlen(event), TNT_MODULE_WRITE_TIMEOUT_MS,
        true);
    if (write_result == MODULE_WRITE_STOPPING) {
        return;
    }
    if (write_result != MODULE_WRITE_OK) {
        fprintf(stderr, "module runtime: disabling %s after write %s\n",
                module->manifest.name,
                write_result == MODULE_WRITE_TIMEOUT ? "timeout" : "failure");
        close_module_process(module);
        return;
    }

    response_deadline = monotonic_millis() + g_module_response_timeout_ms;
    while (1) {
        int64_t remaining;
        int n;

        if (atomic_load_explicit(&g_stop_requested, memory_order_acquire)) {
            return;
        }
        remaining = response_deadline - monotonic_millis();
        if (remaining <= 0) {
            n = 0;
        } else {
            n = read_line_deadline(module->stdout_fd, &module->output, line,
                                   sizeof(line), response_deadline, true);
        }
        if (n == MODULE_READ_STOPPING) {
            return;
        }
        if (atomic_load_explicit(&g_stop_requested, memory_order_acquire)) {
            return;
        }
        if (n == 0) {
            fprintf(stderr,
                    "module runtime: disabling %s after response timeout\n",
                    module->manifest.name);
            close_module_process(module);
            return;
        }
        if (n < 0) {
            fprintf(stderr, "module runtime: disabling %s after read failure\n",
                    module->manifest.name);
            close_module_process(module);
            return;
        }
        responses++;
        if (responses > TNT_MODULE_MAX_RESPONSES_PER_EVENT) {
            fprintf(stderr,
                    "module runtime: disabling %s after too many responses\n",
                    module->manifest.name);
            close_module_process(module);
            return;
        }

        module_response_action_t action = handle_module_response(module, line);
        if (action == MODULE_RESPONSE_DONE) {
            module->invalid_responses = 0;
            return;
        }
        if (action == MODULE_RESPONSE_INVALID) {
            module->invalid_responses++;
            if (module->invalid_responses >= TNT_MODULE_MAX_INVALID_RESPONSES) {
                fprintf(stderr,
                        "module runtime: disabling %s after invalid responses\n",
                        module->manifest.name);
                close_module_process(module);
            }
            return;
        }
        module->invalid_responses = 0;
    }
}

static void *module_worker_main(void *arg) {
    module_process_t *module = arg;
    module_event_t event;

    while (module_queue_pop(module, &event)) {
        deliver_message_to_module(module, &event.msg, event.event_id);
    }

    return NULL;
}

static int load_modules_from_env(void) {
    const char *paths = getenv("TNT_MODULE_PATHS");
    char copy[4096];
    char *saveptr = NULL;
    char *token;

    if (!paths || paths[0] == '\0') {
        return 0;
    }
    if (strlen(paths) >= sizeof(copy)) {
        fprintf(stderr, "module runtime: TNT_MODULE_PATHS too long\n");
        return -1;
    }

    snprintf(copy, sizeof(copy), "%s", paths);
    token = strtok_r(copy, ":", &saveptr);
    while (token && g_module_count < TNT_MAX_MODULES) {
        module_process_t *module = &g_modules[g_module_count];

        memset(module, 0, sizeof(*module));
        module->pid = -1;
        module->pgid = -1;
        module->stdin_fd = -1;
        module->stdout_fd = -1;
        atomic_init(&module->active, false);
        if (tnt_module_manifest_load(token, &module->manifest) == 0 &&
            start_module_process(token, module) == 0) {
            fprintf(stderr, "module runtime: enabled %s\n",
                    module->manifest.name);
            g_module_count++;
        } else {
            fprintf(stderr, "module runtime: failed to enable module at %s\n",
                    token);
        }
        token = strtok_r(NULL, ":", &saveptr);
    }

    return 0;
}

static size_t module_worker_stack_size(void) {
    size_t stack_size = TNT_MODULE_WORKER_STACK_SIZE;

#ifdef PTHREAD_STACK_MIN
    if (stack_size < (size_t)PTHREAD_STACK_MIN) {
        stack_size = (size_t)PTHREAD_STACK_MIN;
    }
#endif
    return stack_size;
}

static void stop_module_workers(int count) {
    for (int i = 0; i < count; i++) {
        module_queue_stop(&g_modules[i]);
    }
    for (int i = 0; i < count; i++) {
        if (g_modules[i].worker_started) {
            pthread_join(g_modules[i].worker, NULL);
            g_modules[i].worker_started = false;
        }
    }
    for (int i = 0; i < count; i++) {
        module_queue_destroy(&g_modules[i]);
    }
}

static int start_module_workers(int count) {
    pthread_attr_t attr;
    bool attr_initialized = false;

    for (int i = 0; i < count; i++) {
        if (module_queue_init(&g_modules[i]) < 0) {
            goto fail;
        }
    }

    if (pthread_attr_init(&attr) != 0) {
        goto fail;
    }
    attr_initialized = true;
    if (pthread_attr_setstacksize(&attr, module_worker_stack_size()) != 0) {
        goto fail;
    }

    for (int i = 0; i < count; i++) {
        if (pthread_create(&g_modules[i].worker, &attr, module_worker_main,
                           &g_modules[i]) != 0) {
            goto fail;
        }
        g_modules[i].worker_started = true;
    }

    pthread_attr_destroy(&attr);
    return 0;

fail:
    if (attr_initialized) {
        pthread_attr_destroy(&attr);
    }
    stop_module_workers(count);
    return -1;
}

static void close_module_processes(int count) {
    for (int i = 0; i < count; i++) {
        close_module_process(&g_modules[i]);
    }
}

int tnt_module_runtime_init(void) {
    int count;

    pthread_mutex_lock(&g_lifecycle_lock);
    pthread_mutex_lock(&g_dispatch_lock);
    if (g_accepting || g_module_count != 0) {
        pthread_mutex_unlock(&g_dispatch_lock);
        pthread_mutex_unlock(&g_lifecycle_lock);
        return -1;
    }
    g_next_event_id = 0;
    g_module_response_timeout_ms =
        tnt_config_env_int(&TNT_CONFIG_MODULE_RESPONSE_TIMEOUT);
    pthread_mutex_unlock(&g_dispatch_lock);

    atomic_store_explicit(&g_stop_requested, false, memory_order_release);
    if (load_modules_from_env() < 0) {
        goto fail;
    }
    count = g_module_count;
    if (count == 0) {
        atomic_store_explicit(&g_stop_requested, true, memory_order_release);
        pthread_mutex_unlock(&g_lifecycle_lock);
        return 0;
    }
    if (start_module_workers(count) < 0) {
        goto fail;
    }

    pthread_mutex_lock(&g_dispatch_lock);
    g_accepting = true;
    pthread_mutex_unlock(&g_dispatch_lock);
    pthread_mutex_unlock(&g_lifecycle_lock);
    return 0;

fail:
    count = g_module_count;
    atomic_store_explicit(&g_stop_requested, true, memory_order_release);
    stop_module_workers(count);
    close_module_processes(count);
    pthread_mutex_lock(&g_dispatch_lock);
    g_module_count = 0;
    pthread_mutex_unlock(&g_dispatch_lock);
    pthread_mutex_unlock(&g_lifecycle_lock);
    return -1;
}

void tnt_module_runtime_shutdown(void) {
    int count;

    pthread_mutex_lock(&g_lifecycle_lock);
    atomic_store_explicit(&g_stop_requested, true, memory_order_release);
    pthread_mutex_lock(&g_dispatch_lock);
    g_accepting = false;
    count = g_module_count;
    pthread_mutex_unlock(&g_dispatch_lock);

    stop_module_workers(count);
    close_module_processes(count);

    pthread_mutex_lock(&g_dispatch_lock);
    g_module_count = 0;
    g_next_event_id = 0;
    pthread_mutex_unlock(&g_dispatch_lock);
    pthread_mutex_unlock(&g_lifecycle_lock);
}

void tnt_module_runtime_publish_message_created(const message_t *msg) {
    char saturated_modules[TNT_MAX_MODULES][TNT_MODULE_NAME_MAX + 1];
    int saturated_count = 0;
    uint64_t event_id;

    if (!msg ||
        atomic_load_explicit(&g_stop_requested, memory_order_acquire)) {
        return;
    }

    pthread_mutex_lock(&g_dispatch_lock);
    if (!g_accepting ||
        atomic_load_explicit(&g_stop_requested, memory_order_acquire)) {
        pthread_mutex_unlock(&g_dispatch_lock);
        return;
    }

    event_id = ++g_next_event_id;
    if (event_id == 0) {
        event_id = ++g_next_event_id;
    }
    for (int i = 0; i < g_module_count; i++) {
        if (module_queue_push(&g_modules[i], msg, event_id) ==
                MODULE_QUEUE_SATURATED &&
            saturated_count < TNT_MAX_MODULES) {
            size_t name_len = strnlen(
                g_modules[i].manifest.name,
                sizeof(saturated_modules[saturated_count]) - 1);

            memcpy(saturated_modules[saturated_count],
                   g_modules[i].manifest.name, name_len);
            saturated_modules[saturated_count][name_len] = '\0';
            saturated_count++;
        }
    }
    pthread_mutex_unlock(&g_dispatch_lock);

    for (int i = 0; i < saturated_count; i++) {
        fprintf(stderr, "module runtime: event queue full for %s, dropping\n",
                saturated_modules[i]);
    }
}

#ifdef TNT_TESTING
/* Deterministic pipe-level coverage for the timeout and shutdown-cancellation
 * paths.  Production callers only reach this writer through module_process. */
int tnt_module_runtime_test_write_fd(int fd, const char *data, size_t len,
                                     int timeout_ms, bool cancel_on_stop) {
    if (set_fd_nonblocking(fd) < 0) {
        return MODULE_WRITE_ERROR;
    }
    return write_module_input(fd, data, len, timeout_ms, cancel_on_stop);
}

int tnt_module_runtime_test_read_fd(int fd, char *line, size_t line_size,
                                    int timeout_ms, bool cancel_on_stop) {
    module_read_buffer_t buffer = {0};

    if (timeout_ms < 0 || set_fd_nonblocking(fd) < 0) {
        return MODULE_READ_ERROR;
    }
    return read_line_deadline(fd, &buffer, line, line_size,
                              monotonic_millis() + timeout_ms,
                              cancel_on_stop);
}

int tnt_module_runtime_test_read_pair_fd(int fd, char *first,
                                         size_t first_size, char *second,
                                         size_t second_size, int timeout_ms) {
    module_read_buffer_t buffer = {0};
    int64_t deadline;
    int n;

    if (timeout_ms < 0 || set_fd_nonblocking(fd) < 0) {
        return MODULE_READ_ERROR;
    }
    deadline = monotonic_millis() + timeout_ms;
    n = read_line_deadline(fd, &buffer, first, first_size, deadline, false);
    if (n <= 0) {
        return n;
    }
    return read_line_deadline(fd, &buffer, second, second_size, deadline,
                              false);
}

void tnt_module_runtime_test_reset_stop(void) {
    atomic_store_explicit(&g_stop_requested, false, memory_order_release);
}
#endif
