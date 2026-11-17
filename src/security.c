#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include "security.h"
#include "json_text.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define SECURITY_LOG_MAX_SIZE (1024 * 1024)

static pthread_mutex_t g_audit_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_uint_fast64_t g_ban_generation;

uint64_t security_ban_generation(void) {
    return atomic_load(&g_ban_generation);
}

static bool write_all(int fd, const char *data, size_t size) {
    while (size) {
        ssize_t written = write(fd, data, size);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        data += written;
        size -= (size_t)written;
    }
    return true;
}

static int ban_path(const char *ip, char *path, size_t size) {
    unsigned char address[16];
    char canonical[INET6_ADDRSTRLEN];
    char name[sizeof(canonical) + 6];
    int family = ip && inet_pton(AF_INET, ip, address) == 1 ? AF_INET : AF_INET6;

    if (!ip || inet_pton(family, ip, address) != 1 ||
        !inet_ntop(family, address, canonical, sizeof(canonical))) {
        errno = EINVAL;
        return -1;
    }
    snprintf(name, sizeof(name), "bans/%s", canonical);
    return tnt_state_path(path, size, name);
}

void security_audit(const char *event, const char *ip, const char *session,
                    const char *nickname, const char *reason) {
    char path[PATH_MAX];
    char backup[PATH_MAX];
    char record[2048] = "";
    char timestamp[32];
    struct tm utc;
    struct stat st;
    time_t now = time(NULL);
    size_t pos = 0;

    if (tnt_state_path(path, sizeof(path), "security.log") < 0 ||
        tnt_state_path(backup, sizeof(backup), "security.log.1") < 0 ||
        !gmtime_r(&now, &utc) ||
        !strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc)) {
        return;
    }
    const char *keys[] = {"timestamp", "event", "ip", "session", "nickname", "reason"};
    const char *values[] = {timestamp, event, ip, session, nickname, reason};
    buffer_appendf(record, sizeof(record), &pos, "{");
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        buffer_appendf(record, sizeof(record), &pos, "%s\"%s\":", i ? "," : "", keys[i]);
        tnt_json_append_string(record, sizeof(record), &pos, values[i]);
    }
    buffer_appendf(record, sizeof(record), &pos, "}\n");

    pthread_mutex_lock(&g_audit_lock);
    if (lstat(path, &st) == 0 && S_ISREG(st.st_mode) &&
        st.st_size + (off_t)pos > SECURITY_LOG_MAX_SIZE &&
        rename(path, backup) < 0) {
        fprintf(stderr, "security: cannot rotate audit log: %s\n", strerror(errno));
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd >= 0) {
        if (fchmod(fd, 0600) < 0 || !write_all(fd, record, pos)) {
            fprintf(stderr, "security: cannot append audit log: %s\n", strerror(errno));
        }
        close(fd);
    } else {
        fprintf(stderr, "security: cannot open audit log: %s\n", strerror(errno));
    }
    pthread_mutex_unlock(&g_audit_lock);
}

int security_is_banned(const char *ip, time_t *until) {
    char path[PATH_MAX];
    char line[128];
    char *end;
    struct stat st;

    if (ban_path(ip, path, sizeof(path)) < 0) return 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return -1;
    }
    ssize_t n = read(fd, line, sizeof(line) - 1);
    close(fd);
    if (n <= 0) return -1;
    line[n] = '\0';
    errno = 0;
    long long expiry = strtoll(line, &end, 10);
    if (errno || end == line || (*end != ' ' && *end != '\n') ||
        expiry < 0 || (long long)(time_t)expiry != expiry) return -1;
    if (until) *until = (time_t)expiry;
    return expiry == 0 || time(NULL) < (time_t)expiry;
}

int security_ban(const char *ip, int seconds, const char *reason) {
    char path[PATH_MAX];
    char directory[PATH_MAX];
    char temporary[PATH_MAX];
    char record[128];
    struct stat st;
    time_t expiry = seconds > 0 ? time(NULL) + seconds : 0;

    if (seconds < 0 || ban_path(ip, path, sizeof(path)) < 0 ||
        tnt_ensure_state_dir() < 0 ||
        tnt_state_path(directory, sizeof(directory), "bans") < 0 ||
        tnt_state_path(temporary, sizeof(temporary), "bans/.ban.XXXXXX") < 0) return -1;
    if (mkdir(directory, 0700) < 0 && errno != EEXIST) return -1;
    if (lstat(directory, &st) < 0 || !S_ISDIR(st.st_mode)) return -1;
    int fd = mkstemp(temporary);
    if (fd < 0) return -1;
    int n = snprintf(record, sizeof(record), "%lld %s\n", (long long)expiry, reason ? reason : "manual");
    bool ok = n > 0 && (size_t)n < sizeof(record) &&
              write_all(fd, record, (size_t)n) && fsync(fd) == 0;
    if (close(fd) < 0) ok = false;
    if (!ok || rename(temporary, path) < 0) {
        unlink(temporary);
        return -1;
    }
    atomic_fetch_add(&g_ban_generation, 1);
    security_audit("ban", ip, "", "", reason);
    fprintf(stderr, "security: banned %s until %lld (%s)\n", ip, (long long)expiry, reason ? reason : "manual");
    return 0;
}

int security_unban(const char *ip) {
    char path[PATH_MAX];

    if (ban_path(ip, path, sizeof(path)) < 0) return -1;
    if (unlink(path) < 0 && errno != ENOENT) return -1;
    atomic_fetch_add(&g_ban_generation, 1);
    security_audit("unban", ip, "", "", "manual");
    return 0;
}

int security_list_bans(FILE *out) {
    char path[PATH_MAX];
    struct dirent *entry;

    if (tnt_state_path(path, sizeof(path), "bans") < 0) return -1;
    DIR *dir = opendir(path);
    if (!dir) return errno == ENOENT ? 0 : -1;
    int rc = 0;
    while ((entry = readdir(dir)) != NULL) {
        time_t until = 0;
        if (entry->d_name[0] == '.') continue;
        int blocked = security_is_banned(entry->d_name, &until);
        if (blocked < 0) {
            fprintf(stderr, "security: invalid ban record for %s\n", entry->d_name);
            rc = -1;
        } else if (blocked && fprintf(out, "%s %lld\n", entry->d_name, (long long)until) < 0) rc = -1;
    }
    closedir(dir);
    return rc;
}
