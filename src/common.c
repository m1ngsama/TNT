#include "common.h"
#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

const char* tnt_state_dir(void) {
    const char *dir = getenv("TNT_STATE_DIR");

    if (!dir || dir[0] == '\0') {
        return TNT_DEFAULT_STATE_DIR;
    }

    return dir;
}

int tnt_ensure_state_dir(void) {
    const char *dir = tnt_state_dir();
    char path[PATH_MAX];
    struct stat st;
    size_t len;

    if (!dir || dir[0] == '\0') {
        return -1;
    }

    if (strcmp(dir, ".") == 0 || strcmp(dir, "/") == 0) {
        return 0;
    }

    if (snprintf(path, sizeof(path), "%s", dir) >= (int)sizeof(path)) {
        return -1;
    }

    len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
        len--;
    }

    for (char *p = path + 1; *p; p++) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (mkdir(path, 0700) < 0 && errno != EEXIST) {
            return -1;
        }
        *p = '/';
    }

    if (mkdir(path, 0700) < 0 && errno != EEXIST) {
        return -1;
    }

    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return -1;
    }

    return 0;
}

int tnt_state_path(char *buffer, size_t buf_size, const char *filename) {
    const char *dir;
    int written;

    if (!buffer || buf_size == 0 || !filename || filename[0] == '\0') {
        return -1;
    }

    dir = tnt_state_dir();

    if (strcmp(dir, "/") == 0) {
        written = snprintf(buffer, buf_size, "/%s", filename);
    } else {
        written = snprintf(buffer, buf_size, "%s/%s", dir, filename);
    }

    if (written < 0 || (size_t)written >= buf_size) {
        return -1;
    }

    return 0;
}

void buffer_append_bytes(char *buffer, size_t buf_size, size_t *pos,
                         const char *data, size_t len) {
    size_t available;
    size_t to_copy;

    if (!buffer || !pos || !data || len == 0 || buf_size == 0 ||
        *pos >= buf_size - 1) {
        return;
    }

    available = (buf_size - 1) - *pos;
    to_copy = (len < available) ? len : available;
    memcpy(buffer + *pos, data, to_copy);
    *pos += to_copy;
    buffer[*pos] = '\0';
}

void buffer_appendf(char *buffer, size_t buf_size, size_t *pos,
                    const char *fmt, ...) {
    va_list args;
    int written;

    if (!buffer || !pos || !fmt || buf_size == 0 || *pos >= buf_size - 1) {
        return;
    }

    va_start(args, fmt);
    written = vsnprintf(buffer + *pos, buf_size - *pos, fmt, args);
    va_end(args);

    if (written < 0) {
        return;
    }

    if ((size_t)written >= buf_size - *pos) {
        *pos = buf_size - 1;
    } else {
        *pos += (size_t)written;
    }
}

int env_int(const char *name, int fallback, int min_val, int max_val) {
    const char *env = getenv(name);
    if (!env || env[0] == '\0') return fallback;
    char *end;
    long val = strtol(env, &end, 10);
    if (*end != '\0' || val < min_val || val > max_val) return fallback;
    return (int)val;
}
