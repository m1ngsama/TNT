#include "module_runtime.h"

#include "chat_room.h"
#include "common.h"
#include "json_text.h"
#include "module_protocol.h"
#include "utf8.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

#define TNT_MODULE_LINE_MAX 4096
#define TNT_MODULE_HANDSHAKE_TIMEOUT_MS 2000
#define TNT_MODULE_RESPONSE_TIMEOUT_MS 100

struct client;
void notify_mentions(const char *content, const struct client *sender);

typedef struct module_process {
    tnt_module_manifest_t manifest;
    pid_t pid;
    int stdin_fd;
    int stdout_fd;
    bool active;
} module_process_t;

typedef struct module_event_node {
    message_t msg;
    struct module_event_node *next;
} module_event_node_t;

static module_process_t g_modules[TNT_MAX_MODULES];
static int g_module_count = 0;
static pthread_t g_module_thread;
static bool g_thread_started = false;
static bool g_running = false;
static pthread_mutex_t g_queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_queue_cond = PTHREAD_COND_INITIALIZER;
static module_event_node_t *g_queue_head = NULL;
static module_event_node_t *g_queue_tail = NULL;
static int g_queue_len = 0;

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
        !is_valid_username(out->name) ||
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

static int wait_fd_readable(int fd, int timeout_ms) {
    fd_set readfds;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    return select(fd + 1, &readfds, NULL, NULL, &tv);
}

static int read_line_timeout(int fd, char *line, size_t line_size,
                             int timeout_ms) {
    size_t pos = 0;

    if (!line || line_size == 0) return -1;
    line[0] = '\0';

    while (pos + 1 < line_size) {
        char c;
        int ready = wait_fd_readable(fd, timeout_ms);
        if (ready <= 0) {
            break;
        }
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) {
            return -1;
        }
        if (c == '\n') {
            line[pos] = '\0';
            return (int)pos;
        }
        if ((unsigned char)c < 32 && c != '\t' && c != '\r') {
            return -1;
        }
        line[pos++] = c;
    }

    line[pos] = '\0';
    return pos > 0 ? (int)pos : 0;
}

static void close_module_process(module_process_t *module) {
    if (!module || !module->active) return;

    close(module->stdin_fd);
    close(module->stdout_fd);
    kill(module->pid, SIGTERM);
    waitpid(module->pid, NULL, WNOHANG);
    module->active = false;
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
        close(in_pipe[1]);
        close(out_pipe[0]);
        if (dup2(in_pipe[0], STDIN_FILENO) < 0 ||
            dup2(out_pipe[1], STDOUT_FILENO) < 0 ||
            chdir(module_dir) < 0) {
            _exit(127);
        }
        close(in_pipe[0]);
        close(out_pipe[1]);
        execl(module->manifest.entrypoint, module->manifest.entrypoint,
              (char *)NULL);
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);

    module->pid = pid;
    module->stdin_fd = in_pipe[1];
    module->stdout_fd = out_pipe[0];
    module->active = true;

    if (tnt_module_append_handshake(handshake, sizeof(handshake), &pos,
                                    TNT_VERSION) < 0 ||
        write(module->stdin_fd, handshake, strlen(handshake)) !=
            (ssize_t)strlen(handshake) ||
        read_line_timeout(module->stdout_fd, line, sizeof(line),
                          TNT_MODULE_HANDSHAKE_TIMEOUT_MS) <= 0 ||
        !handshake_ok(line)) {
        close_module_process(module);
        return -1;
    }

    return 0;
}

static void enqueue_message(const message_t *msg) {
    module_event_node_t *node;

    if (!msg) return;

    pthread_mutex_lock(&g_queue_lock);
    if (!g_running || g_queue_len >= TNT_MODULE_QUEUE_LIMIT) {
        pthread_mutex_unlock(&g_queue_lock);
        if (g_queue_len >= TNT_MODULE_QUEUE_LIMIT) {
            fprintf(stderr, "module runtime: event queue full, dropping\n");
        }
        return;
    }

    node = calloc(1, sizeof(*node));
    if (!node) {
        pthread_mutex_unlock(&g_queue_lock);
        return;
    }
    node->msg = *msg;

    if (g_queue_tail) {
        g_queue_tail->next = node;
    } else {
        g_queue_head = node;
    }
    g_queue_tail = node;
    g_queue_len++;
    pthread_cond_signal(&g_queue_cond);
    pthread_mutex_unlock(&g_queue_lock);
}

static module_event_node_t *dequeue_message(void) {
    module_event_node_t *node;

    pthread_mutex_lock(&g_queue_lock);
    while (g_running && !g_queue_head) {
        pthread_cond_wait(&g_queue_cond, &g_queue_lock);
    }
    node = g_queue_head;
    if (node) {
        g_queue_head = node->next;
        if (!g_queue_head) g_queue_tail = NULL;
        g_queue_len--;
    }
    pthread_mutex_unlock(&g_queue_lock);
    return node;
}

static void publish_module_message(const module_process_t *module,
                                   const char *plain_text) {
    message_t msg = {
        .timestamp = time(NULL),
    };

    if (!module || !plain_text || plain_text[0] == '\0') return;

    snprintf(msg.username, sizeof(msg.username), "module:%s",
             module->manifest.name);
    snprintf(msg.content, sizeof(msg.content), "%s", plain_text);

    if (message_save(&msg) < 0) {
        fprintf(stderr, "module runtime: failed to persist module message\n");
        return;
    }

    room_broadcast(g_room, &msg);
    notify_mentions(msg.content, NULL);
}

static void handle_module_response(module_process_t *module, const char *line) {
    tnt_module_message_create_t create;
    char type[64];

    if (!module || !line || line[0] == '\0') return;

    if (tnt_module_parse_message_create(line, &create)) {
        publish_module_message(module, create.plain_text);
        return;
    }
    if (tnt_json_get_string_field(line, "type", type, sizeof(type)) &&
        strcmp(type, "event.ok") == 0) {
        return;
    }

    fprintf(stderr, "module runtime: ignored invalid response from %s\n",
            module->manifest.name);
}

static void deliver_message_to_module(module_process_t *module,
                                      const message_t *msg,
                                      uint64_t event_id) {
    char event[TNT_MODULE_LINE_MAX] = "";
    char line[TNT_MODULE_LINE_MAX];
    char message_id[64];
    size_t pos = 0;

    if (!module || !module->active || !msg) return;

    snprintf(message_id, sizeof(message_id), "local-%llu",
             (unsigned long long)event_id);
    if (tnt_module_append_message_created(event, sizeof(event), &pos,
                                          message_id, msg) < 0 ||
        write(module->stdin_fd, event, strlen(event)) !=
            (ssize_t)strlen(event)) {
        fprintf(stderr, "module runtime: disabling %s after write failure\n",
                module->manifest.name);
        close_module_process(module);
        return;
    }

    while (1) {
        int n = read_line_timeout(module->stdout_fd, line, sizeof(line),
                                  TNT_MODULE_RESPONSE_TIMEOUT_MS);
        if (n == 0) {
            return;
        }
        if (n < 0) {
            fprintf(stderr, "module runtime: disabling %s after read failure\n",
                    module->manifest.name);
            close_module_process(module);
            return;
        }
        handle_module_response(module, line);
    }
}

static void *module_worker_main(void *arg) {
    uint64_t event_id = 0;
    (void)arg;

    while (g_running) {
        module_event_node_t *node = dequeue_message();
        if (!node) {
            continue;
        }

        event_id++;
        for (int i = 0; i < g_module_count; i++) {
            deliver_message_to_module(&g_modules[i], &node->msg, event_id);
        }
        free(node);
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
        module->stdin_fd = -1;
        module->stdout_fd = -1;
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

int tnt_module_runtime_init(void) {
    g_module_count = 0;
    g_running = false;

    if (load_modules_from_env() < 0) {
        return -1;
    }
    if (g_module_count == 0) {
        return 0;
    }

    g_running = true;
    if (pthread_create(&g_module_thread, NULL, module_worker_main, NULL) != 0) {
        g_running = false;
        for (int i = 0; i < g_module_count; i++) {
            close_module_process(&g_modules[i]);
        }
        g_module_count = 0;
        return -1;
    }
    g_thread_started = true;
    return 0;
}

void tnt_module_runtime_shutdown(void) {
    module_event_node_t *node;

    pthread_mutex_lock(&g_queue_lock);
    g_running = false;
    pthread_cond_broadcast(&g_queue_cond);
    pthread_mutex_unlock(&g_queue_lock);

    if (g_thread_started) {
        pthread_join(g_module_thread, NULL);
        g_thread_started = false;
    }

    while ((node = g_queue_head) != NULL) {
        g_queue_head = node->next;
        free(node);
    }
    g_queue_tail = NULL;
    g_queue_len = 0;

    for (int i = 0; i < g_module_count; i++) {
        close_module_process(&g_modules[i]);
    }
    g_module_count = 0;
}

void tnt_module_runtime_publish_message_created(const message_t *msg) {
    if (!msg || g_module_count == 0) {
        return;
    }

    enqueue_message(msg);
}
