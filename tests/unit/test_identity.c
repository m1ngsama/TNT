#include "../../include/identity.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    test_##name(); \
    printf("ok\n"); \
    tests_passed++; \
} while (0)

/* Generated with `ssh-keygen -t ed25519`; `ssh-keygen -lf` prints KEY_SHA256. */
#define KEY_BASE64 \
    "AAAAC3NzaC1lZDI1NTE5AAAAIH8bmE4NEMDW8dBlYJ2ADZ1zmtMmJpNfWMd1yKnpNjtC"
#define KEY_SHA256 "SHA256:UMSdY3XS0sbuWW4MC+rV6SEa0OanWxNxnFV35VM5Sng"
#define OTHER_SHA256 "SHA256:47DEQpj8HBSa+/TImW+5JCeuQeRkm5NMpJWZG3hSuFU"

static int tests_passed = 0;
static char state_dir[PATH_MAX];

static void write_text(const char *path, const char *text) {
    FILE *fp = fopen(path, "w");

    assert(fp);
    assert(fputs(text, fp) >= 0);
    assert(fclose(fp) == 0);
}

static void read_text(const char *path, char *out, size_t out_size) {
    FILE *fp = fopen(path, "r");
    size_t n;

    assert(fp);
    n = fread(out, 1, out_size - 1, fp);
    out[n] = '\0';
    fclose(fp);
}

static void state_path(char *out, size_t out_size, const char *name) {
    snprintf(out, out_size, "%s/%s", state_dir, name);
}

TEST(validates_handles) {
    assert(identity_handle_valid("alice"));
    assert(identity_handle_valid("a_1"));
    assert(identity_handle_valid("abcdefghijklmnopqrst"));
    assert(!identity_handle_valid(""));
    assert(!identity_handle_valid("abcdefghijklmnopqrstu"));
    assert(!identity_handle_valid("Alice"));
    assert(!identity_handle_valid("al-ice"));
    assert(!identity_handle_valid("al ice"));
    assert(!identity_handle_valid("用户"));
}

TEST(validates_fingerprints) {
    assert(identity_fingerprint_valid(KEY_SHA256));
    assert(!identity_fingerprint_valid("SHA256:"));
    assert(!identity_fingerprint_valid(
        "MD5:UMSdY3XS0sbuWW4MC+rV6SEa0OanWxNxnFV35VM5Sng"));
    assert(!identity_fingerprint_valid(KEY_SHA256 "="));
    assert(!identity_fingerprint_valid(
        "SHA256:UMSdY3XS0sbuWW4MC+rV6SEa0OanWxNxnFV35VM5Sn"));
    assert(!identity_fingerprint_valid(
        "SHA256:UMSdY3XS0sbuWW4MC-rV6SEa0OanWxNxnFV35VM5Sng"));
}

TEST(formats_fingerprints_like_ssh_keygen) {
    ssh_key key = NULL;
    char fingerprint[TNT_IDENTITY_FINGERPRINT_SIZE];

    assert(ssh_pki_import_pubkey_base64(KEY_BASE64, SSH_KEYTYPE_ED25519,
                                        &key) == SSH_OK);
    assert(identity_fingerprint_from_key(key, fingerprint,
                                         sizeof(fingerprint)) == 0);
    assert(strcmp(fingerprint, KEY_SHA256) == 0);
    assert(identity_fingerprint_from_key(key, fingerprint, 20) < 0);
    ssh_key_free(key);
}

TEST(parses_and_skips_invalid_lines) {
    char path[PATH_MAX];
    identity_table_t table;

    state_path(path, sizeof(path), "parse");
    write_text(path,
               "alice " KEY_SHA256 " not-a-key\n"
               "\n"
               "# comment\n"
               "Bob\n"
               "toolongtoolongtoolong1\n"
               "carol\t" OTHER_SHA256 "\n"
               "dave " KEY_SHA256 "\n"
               "alice " OTHER_SHA256 "\n");
    assert(identity_table_load(&table, path) == 0);
    assert(table.count == 3);
    assert(strcmp(table.entries[0].handle, "alice") == 0);
    assert(table.entries[0].key_count == 1);
    assert(strcmp(table.entries[1].handle, "carol") == 0);
    assert(table.entries[1].key_count == 1);
    assert(strcmp(table.entries[2].handle, "dave") == 0);
    assert(table.entries[2].key_count == 0);
    assert(identity_table_find(&table, "ALICE") == &table.entries[0]);
    assert(identity_table_find(&table, "bob") == NULL);
    assert(strcmp(identity_table_handle_for_key(&table, KEY_SHA256),
                  "alice") == 0);
    identity_table_free(&table);
}

TEST(missing_file_is_empty) {
    char path[PATH_MAX];
    identity_table_t table;

    state_path(path, sizeof(path), "missing");
    assert(identity_table_load(&table, path) == 0);
    assert(table.count == 0);
    identity_table_free(&table);
}

TEST(adds_moves_and_forgets_keys) {
    identity_table_t table = {0};

    assert(identity_table_add(&table, "alice", KEY_SHA256) == 0);
    assert(identity_table_add(&table, "bob", NULL) == 0);
    assert(identity_table_add(&table, "Bad", NULL) < 0);
    assert(identity_table_add(&table, "bob", "SHA256:x") < 0);
    assert(identity_table_add(&table, "bob", KEY_SHA256) == 0);
    assert(table.count == 2);
    assert(table.entries[0].key_count == 0);
    assert(strcmp(identity_table_handle_for_key(&table, KEY_SHA256),
                  "bob") == 0);
    assert(identity_table_forget_key(&table, KEY_SHA256));
    assert(!identity_table_forget_key(&table, KEY_SHA256));
    assert(identity_table_handle_for_key(&table, KEY_SHA256) == NULL);
    assert(identity_table_find(&table, "bob") != NULL);
    identity_table_free(&table);
}

TEST(keeps_the_newest_keys) {
    identity_table_t table = {0};
    char key[TNT_IDENTITY_FINGERPRINT_SIZE];

    for (int i = 0; i <= TNT_IDENTITY_MAX_KEYS; i++) {
        snprintf(key, sizeof(key), "SHA256:%043d", i);
        assert(identity_table_add(&table, "alice", key) == 0);
    }
    assert(table.entries[0].key_count == TNT_IDENTITY_MAX_KEYS);
    snprintf(key, sizeof(key), "SHA256:%043d", 0);
    assert(identity_table_handle_for_key(&table, key) == NULL);
    snprintf(key, sizeof(key), "SHA256:%043d", TNT_IDENTITY_MAX_KEYS);
    assert(identity_table_handle_for_key(&table, key) != NULL);
    identity_table_free(&table);
}

TEST(saves_atomically_with_mode_0600) {
    char path[PATH_MAX];
    char text[512];
    identity_table_t table = {0};
    struct stat st;

    state_path(path, sizeof(path), "saved");
    assert(identity_table_add(&table, "alice", KEY_SHA256) == 0);
    assert(identity_table_add(&table, "alice", OTHER_SHA256) == 0);
    assert(identity_table_add(&table, "bob", NULL) == 0);
    assert(identity_table_save(&table, path) == 0);
    identity_table_free(&table);

    assert(stat(path, &st) == 0);
    assert((st.st_mode & 0777) == 0600);
    read_text(path, text, sizeof(text));
    assert(strcmp(text, "alice " KEY_SHA256 " " OTHER_SHA256 "\nbob\n") == 0);
}

TEST(store_reserves_and_reloads_on_change) {
    char path[PATH_MAX];
    char temp[PATH_MAX + 8];
    char handle[TNT_IDENTITY_HANDLE_MAX + 1];

    state_path(path, sizeof(path), TNT_IDENTITY_FILE);
    assert(!identity_is_reserved("alice"));
    assert(identity_reserve("alice", KEY_SHA256) == 0);
    assert(identity_is_reserved("Alice"));
    assert(identity_handle_for_key(KEY_SHA256, handle, sizeof(handle)));
    assert(strcmp(handle, "alice") == 0);

    assert(identity_forget_key(KEY_SHA256) == 0);
    assert(!identity_handle_for_key(KEY_SHA256, handle, sizeof(handle)));
    assert(identity_is_reserved("alice"));

    /* Another process rewrites the file the same way TNT does. */
    snprintf(temp, sizeof(temp), "%s.new", path);
    write_text(temp, "carol " OTHER_SHA256 "\n");
    assert(rename(temp, path) == 0);
    assert(!identity_is_reserved("alice"));
    assert(identity_is_reserved("carol"));
    assert(identity_handle_for_key(OTHER_SHA256, handle, sizeof(handle)));
}

int main(void) {
    char template[] = "/tmp/tnt-identity-test.XXXXXX";

    printf("Running identity unit tests...\n\n");
    assert(mkdtemp(template));
    snprintf(state_dir, sizeof(state_dir), "%s", template);
    setenv("TNT_STATE_DIR", state_dir, 1);

    RUN_TEST(validates_handles);
    RUN_TEST(validates_fingerprints);
    RUN_TEST(formats_fingerprints_like_ssh_keygen);
    RUN_TEST(parses_and_skips_invalid_lines);
    RUN_TEST(missing_file_is_empty);
    RUN_TEST(adds_moves_and_forgets_keys);
    RUN_TEST(keeps_the_newest_keys);
    RUN_TEST(saves_atomically_with_mode_0600);
    RUN_TEST(store_reserves_and_reloads_on_change);

    for (size_t i = 0; i < 3; i++) {
        static const char *const files[] = {"parse", "saved",
                                             TNT_IDENTITY_FILE};
        char path[PATH_MAX];

        state_path(path, sizeof(path), files[i]);
        unlink(path);
    }
    rmdir(state_dir);
    printf("\nAll %d identity tests passed.\n", tests_passed);
    return 0;
}
