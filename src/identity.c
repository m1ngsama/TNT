#include "identity.h"

#include <errno.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define FINGERPRINT_PREFIX "SHA256:"
#define FINGERPRINT_PREFIX_LEN 7
#define FINGERPRINT_BASE64_LEN 43

static pthread_mutex_t g_identity_lock = PTHREAD_MUTEX_INITIALIZER;
static identity_table_t g_identities;
static bool g_identities_loaded = false;
static struct stat g_identities_stat;
static bool g_identities_exist = false;

bool identity_handle_valid(const char *handle) {
    size_t len;

    if (!handle) {
        return false;
    }
    len = strlen(handle);
    if (len == 0 || len > TNT_IDENTITY_HANDLE_MAX) {
        return false;
    }
    for (const char *p = handle; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
              *p == '_')) {
            return false;
        }
    }
    return true;
}

bool identity_fingerprint_valid(const char *fingerprint) {
    if (!fingerprint ||
        strncmp(fingerprint, FINGERPRINT_PREFIX, FINGERPRINT_PREFIX_LEN) != 0 ||
        strlen(fingerprint) !=
            FINGERPRINT_PREFIX_LEN + FINGERPRINT_BASE64_LEN) {
        return false;
    }
    for (const char *p = fingerprint + FINGERPRINT_PREFIX_LEN; *p; p++) {
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') || *p == '+' || *p == '/')) {
            return false;
        }
    }
    return true;
}

int identity_fingerprint_from_key(ssh_key key, char *out, size_t out_size) {
    unsigned char *hash = NULL;
    size_t hash_len = 0;
    char *fingerprint;
    int rc = -1;

    if (!key || !out || out_size == 0) {
        return -1;
    }
    out[0] = '\0';
    if (ssh_get_publickey_hash(key, SSH_PUBLICKEY_HASH_SHA256, &hash,
                               &hash_len) != 0) {
        return -1;
    }
    fingerprint = ssh_get_fingerprint_hash(SSH_PUBLICKEY_HASH_SHA256, hash,
                                           hash_len);
    ssh_clean_pubkey_hash(&hash);
    if (fingerprint && identity_fingerprint_valid(fingerprint) &&
        strlen(fingerprint) < out_size) {
        snprintf(out, out_size, "%s", fingerprint);
        rc = 0;
    }
    ssh_string_free_char(fingerprint);
    return rc;
}

void identity_table_free(identity_table_t *table) {
    if (!table) {
        return;
    }
    free(table->entries);
    table->entries = NULL;
    table->count = 0;
}

const identity_entry_t *identity_table_find(const identity_table_t *table,
                                            const char *name) {
    if (!table || !name) {
        return NULL;
    }
    for (size_t i = 0; i < table->count; i++) {
        if (strcasecmp(table->entries[i].handle, name) == 0) {
            return &table->entries[i];
        }
    }
    return NULL;
}

const char *identity_table_handle_for_key(const identity_table_t *table,
                                          const char *fingerprint) {
    if (!table || !fingerprint || fingerprint[0] == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < table->count; i++) {
        for (int k = 0; k < table->entries[i].key_count; k++) {
            if (strcmp(table->entries[i].keys[k], fingerprint) == 0) {
                return table->entries[i].handle;
            }
        }
    }
    return NULL;
}

static void entry_remove_key(identity_entry_t *entry, int index) {
    memmove(entry->keys[index], entry->keys[index + 1],
            (size_t)(entry->key_count - index - 1) * sizeof(entry->keys[0]));
    entry->key_count--;
}

bool identity_table_forget_key(identity_table_t *table,
                               const char *fingerprint) {
    bool removed = false;

    if (!table || !fingerprint) {
        return false;
    }
    for (size_t i = 0; i < table->count; i++) {
        identity_entry_t *entry = &table->entries[i];
        for (int k = entry->key_count - 1; k >= 0; k--) {
            if (strcmp(entry->keys[k], fingerprint) == 0) {
                entry_remove_key(entry, k);
                removed = true;
            }
        }
    }
    return removed;
}

int identity_table_add(identity_table_t *table, const char *handle,
                       const char *fingerprint) {
    identity_entry_t *entry;

    if (!table || !identity_handle_valid(handle) ||
        (fingerprint && !identity_fingerprint_valid(fingerprint))) {
        return -1;
    }

    entry = (identity_entry_t *)identity_table_find(table, handle);
    if (!entry) {
        identity_entry_t *grown = realloc(
            table->entries, (table->count + 1) * sizeof(*table->entries));
        if (!grown) {
            return -1;
        }
        table->entries = grown;
        entry = &table->entries[table->count++];
        memset(entry, 0, sizeof(*entry));
        snprintf(entry->handle, sizeof(entry->handle), "%s", handle);
    }

    if (fingerprint) {
        (void)identity_table_forget_key(table, fingerprint);
        if (entry->key_count == TNT_IDENTITY_MAX_KEYS) {
            entry_remove_key(entry, 0);
        }
        snprintf(entry->keys[entry->key_count++], sizeof(entry->keys[0]),
                 "%s", fingerprint);
    }
    return 0;
}

static int parse_line(identity_table_t *table, char *line) {
    char *saveptr = NULL;
    char *token = strtok_r(line, " \t\r\n", &saveptr);

    if (!token || !identity_handle_valid(token) ||
        identity_table_find(table, token)) {
        return 0;
    }
    if (identity_table_add(table, token, NULL) < 0) {
        return -1;
    }
    char handle[TNT_IDENTITY_HANDLE_MAX + 1];
    snprintf(handle, sizeof(handle), "%s", token);
    while ((token = strtok_r(NULL, " \t\r\n", &saveptr)) != NULL) {
        if (identity_fingerprint_valid(token) &&
            !identity_table_handle_for_key(table, token) &&
            identity_table_add(table, handle, token) < 0) {
            return -1;
        }
    }
    return 0;
}

int identity_table_load(identity_table_t *table, const char *path) {
    FILE *fp;
    char *line = NULL;
    size_t line_size = 0;
    int rc = 0;

    if (!table || !path) {
        return -1;
    }
    table->entries = NULL;
    table->count = 0;

    fp = fopen(path, "r");
    if (!fp) {
        return errno == ENOENT ? 0 : -1;
    }
    while (getline(&line, &line_size, fp) >= 0) {
        if (parse_line(table, line) < 0) {
            rc = -1;
            break;
        }
    }
    if (ferror(fp)) {
        rc = -1;
    }
    free(line);
    fclose(fp);
    if (rc < 0) {
        identity_table_free(table);
    }
    return rc;
}

int identity_table_save(const identity_table_t *table, const char *path) {
    char temp_path[PATH_MAX];
    FILE *fp;
    int fd;
    bool ok = true;

    if (!table || !path ||
        snprintf(temp_path, sizeof(temp_path), "%s.XXXXXX", path) >=
            (int)sizeof(temp_path)) {
        return -1;
    }

    fd = mkstemp(temp_path);
    if (fd < 0) {
        return -1;
    }
    fp = fdopen(fd, "w");
    if (!fp) {
        close(fd);
        unlink(temp_path);
        return -1;
    }

    for (size_t i = 0; i < table->count && ok; i++) {
        const identity_entry_t *entry = &table->entries[i];

        ok = fputs(entry->handle, fp) >= 0;
        for (int k = 0; k < entry->key_count && ok; k++) {
            ok = fprintf(fp, " %s", entry->keys[k]) >= 0;
        }
        ok = ok && fputc('\n', fp) != EOF;
    }
    ok = ok && fchmod(fd, 0600) == 0 && fflush(fp) == 0 && fsync(fd) == 0;
    if (fclose(fp) != 0) {
        ok = false;
    }
    if (!ok || rename(temp_path, path) != 0) {
        unlink(temp_path);
        return -1;
    }
    return 0;
}

static bool same_file_state(const struct stat *a, const struct stat *b) {
    return a->st_ino == b->st_ino && a->st_size == b->st_size &&
           a->st_mtime == b->st_mtime;
}

static int identities_path(char *path, size_t path_size) {
    return tnt_state_path(path, path_size, TNT_IDENTITY_FILE);
}

/* Caller holds g_identity_lock. */
static int refresh_locked(const char *path) {
    struct stat st;
    bool exists = stat(path, &st) == 0;
    identity_table_t fresh;

    if (g_identities_loaded && exists == g_identities_exist &&
        (!exists || same_file_state(&st, &g_identities_stat))) {
        return 0;
    }
    if (identity_table_load(&fresh, path) < 0) {
        fprintf(stderr, "identity: failed to read %s\n", path);
        return -1;
    }
    identity_table_free(&g_identities);
    g_identities = fresh;
    g_identities_loaded = true;
    g_identities_exist = exists;
    if (exists) {
        g_identities_stat = st;
    }
    return 0;
}

static int save_locked(const char *path) {
    if (identity_table_save(&g_identities, path) < 0) {
        fprintf(stderr, "identity: failed to write %s\n", path);
        g_identities_loaded = false;
        return -1;
    }
    g_identities_exist = stat(path, &g_identities_stat) == 0;
    return 0;
}

bool identity_is_reserved(const char *name) {
    char path[PATH_MAX];
    bool reserved = false;

    if (!name || name[0] == '\0' || identities_path(path, sizeof(path)) < 0) {
        return false;
    }
    pthread_mutex_lock(&g_identity_lock);
    (void)refresh_locked(path);
    reserved = identity_table_find(&g_identities, name) != NULL;
    pthread_mutex_unlock(&g_identity_lock);
    return reserved;
}

bool identity_handle_for_key(const char *fingerprint, char *handle,
                             size_t handle_size) {
    char path[PATH_MAX];
    const char *found;

    if (!fingerprint || fingerprint[0] == '\0' || !handle ||
        handle_size == 0 || identities_path(path, sizeof(path)) < 0) {
        return false;
    }
    pthread_mutex_lock(&g_identity_lock);
    (void)refresh_locked(path);
    found = identity_table_handle_for_key(&g_identities, fingerprint);
    if (found) {
        snprintf(handle, handle_size, "%s", found);
    }
    pthread_mutex_unlock(&g_identity_lock);
    return found != NULL;
}

int identity_reserve(const char *handle, const char *fingerprint) {
    char path[PATH_MAX];
    int rc = -1;

    if (identities_path(path, sizeof(path)) < 0) {
        return -1;
    }
    pthread_mutex_lock(&g_identity_lock);
    if (refresh_locked(path) == 0 &&
        identity_table_add(&g_identities, handle, fingerprint) == 0) {
        rc = save_locked(path);
    }
    pthread_mutex_unlock(&g_identity_lock);
    return rc;
}

int identity_forget_key(const char *fingerprint) {
    char path[PATH_MAX];
    int rc = -1;

    if (!fingerprint || identities_path(path, sizeof(path)) < 0) {
        return -1;
    }
    pthread_mutex_lock(&g_identity_lock);
    if (refresh_locked(path) == 0) {
        rc = identity_table_forget_key(&g_identities, fingerprint)
                 ? save_locked(path)
                 : 0;
    }
    pthread_mutex_unlock(&g_identity_lock);
    return rc;
}
