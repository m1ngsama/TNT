#include "common.h"
#include <errno.h>
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
