#ifndef IDENTITY_H
#define IDENTITY_H

#include "common.h"
#include <libssh/libssh.h>

#define TNT_IDENTITY_FILE "identities"
#define TNT_IDENTITY_HANDLE_MAX 20
/* "SHA256:", 43 unpadded base64 characters, and the NUL byte. */
#define TNT_IDENTITY_FINGERPRINT_SIZE 51
#define TNT_IDENTITY_MAX_KEYS 16

typedef struct {
    char handle[TNT_IDENTITY_HANDLE_MAX + 1];
    int key_count;
    char keys[TNT_IDENTITY_MAX_KEYS][TNT_IDENTITY_FINGERPRINT_SIZE];
} identity_entry_t;

/* The identities file: one handle per line followed by zero or more SSH key
 * fingerprints, all separated by spaces. */
typedef struct {
    identity_entry_t *entries;
    size_t count;
} identity_table_t;

bool identity_handle_valid(const char *handle);
bool identity_fingerprint_valid(const char *fingerprint);

/* Format `key` the way `ssh-keygen -l` does: "SHA256:" and unpadded base64. */
int identity_fingerprint_from_key(ssh_key key, char *out, size_t out_size);

/* A missing file loads as an empty table.  Invalid lines and fingerprints are
 * skipped. */
int identity_table_load(identity_table_t *table, const char *path);

/* Replace `path` atomically with a mode 0600 file. */
int identity_table_save(const identity_table_t *table, const char *path);

void identity_table_free(identity_table_t *table);

/* Match `name` against handles without regard to ASCII letter case. */
const identity_entry_t *identity_table_find(const identity_table_t *table,
                                            const char *name);
const char *identity_table_handle_for_key(const identity_table_t *table,
                                          const char *fingerprint);

/* Add `handle` if absent.  A non-NULL `fingerprint` moves to this handle from
 * any other one; past TNT_IDENTITY_MAX_KEYS the oldest key is dropped. */
int identity_table_add(identity_table_t *table, const char *handle,
                       const char *fingerprint);
bool identity_table_forget_key(identity_table_t *table,
                               const char *fingerprint);

/* The process-wide identities file in the state directory.  Each call
 * reloads the file when it changed on disk. */
bool identity_is_reserved(const char *name);
bool identity_handle_for_key(const char *fingerprint, char *handle,
                             size_t handle_size);
int identity_reserve(const char *handle, const char *fingerprint);
int identity_forget_key(const char *fingerprint);

#endif /* IDENTITY_H */
