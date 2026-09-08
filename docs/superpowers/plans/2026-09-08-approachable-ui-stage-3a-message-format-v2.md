# Approachable UI — Stage 3a: Message Format v2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a stored message contain a newline, and migrate existing logs to the new encoding exactly once, safely.

**Architecture:** `message_log.c` holds the only record parser and the only record writer, so encoding and decoding go there and every consumer inherits them. In memory a message holds decoded text; on disk it is escaped. A header line marks a migrated file so the marker can never be separated from the data it describes.

**Tech Stack:** C11, no new dependencies.

**Spec:** `docs/superpowers/specs/2026-09-07-approachable-ui-design.md` (Stage 3, format half)

## Why this ships alone

This is the only irreversible step in the programme. Nothing in this stage can
produce a newline — the input is still single-line — so the format change lands
with **no user-visible behaviour change** and can be verified, deployed, and
rolled back on its own. Multi-line composition is Stage 3b.

## The encoding

Inside the content field: `\` becomes `\\`, and a newline becomes `\n`.
Records stay one per line with exactly two `|` separators, so the file shape,
the field rules, and every script that reads `tntctl dump` are unchanged.

## Why a migration is needed at all

An existing record may already contain a backslash — `C:\new`, or a user who
typed `\n` literally. Decoding every record with v2 rules would turn `C:\new`
into `C:<newline>ew`: silent corruption. Deployments installed from Homebrew,
AUR, or Debian have logs nobody can inspect first.

## Why the marker is a header line, not a sibling file

A sibling marker file can be separated from the log by a copy or a partial
restore, and the next start would escape an already-escaped file. A header line
travels with the data. Both the old and the new parser treat it as an
unparseable record and skip it, which is what makes it safe to add.

## Global Constraints

- C11, `-Wall -Wextra`, no new third-party dependencies.
- **Only `\n` is admitted into content.** Every other C0 and C1 control character and DEL stays rejected, for client messages and module messages alike. Usernames still reject all control characters including `\n`.
- The 1023-byte content limit applies to the **encoded** form, so the parser contract is unchanged.
- `tntctl dump` and `tail` keep emitting the escaped, one-record-per-line form.
- Migration must be atomic, must back up, and must be idempotent.
- `make release-check` green; lands through a pull request.

---

### Task 1: Encode and decode in the shared record layer

**Files:** `include/message_log.h`, `src/message_log.c`, `tests/unit/test_message_log.c` (new), `tests/unit/Makefile`, `MAINTAINERS`, `.gitignore`

**Interfaces:**
- `#define MESSAGE_LOG_HEADER "#tnt-message-log v2"`
- `bool message_log_is_header(const char *line)`
- `bool message_log_encode_content(const char *in, char *out, size_t out_size)` — false when the encoded form does not fit
- `bool message_log_decode_content(const char *in, char *out, size_t out_size)` — false on a malformed escape
- `message_log_parse_record` decodes content; `message_log_format_record` encodes it

- [ ] **Step 1: Write the failing tests**

```c
TEST(encode_escapes_backslash_and_newline) {
    char out[64];
    assert(message_log_encode_content("a\\b", out, sizeof(out)));
    assert(strcmp(out, "a\\\\b") == 0);
    assert(message_log_encode_content("one\ntwo", out, sizeof(out)));
    assert(strcmp(out, "one\\ntwo") == 0);
}

TEST(decode_is_the_inverse_of_encode) {
    const char *cases[] = {"plain", "a\\b", "one\ntwo", "\\n", "\\\\",
                           "中文\nemoji😌", NULL};
    for (int i = 0; cases[i]; i++) {
        char enc[512];
        char dec[512];
        assert(message_log_encode_content(cases[i], enc, sizeof(enc)));
        assert(message_log_decode_content(enc, dec, sizeof(dec)));
        assert(strcmp(dec, cases[i]) == 0);
    }
}

TEST(encode_rejects_overflow) {
    char out[8];
    assert(!message_log_encode_content("\\\\\\\\\\\\\\\\", out, sizeof(out)));
}

TEST(record_round_trip_carries_a_newline) {
    message_t msg = {0};
    message_t back = {0};
    char line[MESSAGE_LOG_MAX_LINE];

    msg.timestamp = 1704067200;
    snprintf(msg.username, sizeof(msg.username), "alice");
    snprintf(msg.content, sizeof(msg.content), "first\nsecond");

    assert(message_log_format_record(&msg, line, sizeof(line), NULL) == 0);
    assert(strstr(line, "first\\nsecond") != NULL);   /* stored escaped */
    assert(strchr(line, '\n') == line + strlen(line) - 1);  /* one line */
    assert(message_log_parse_record(line, &back, msg.timestamp));
    assert(strcmp(back.content, "first\nsecond") == 0);  /* decoded back */
}

TEST(record_still_rejects_other_control_characters) {
    message_t msg = {0};
    char line[MESSAGE_LOG_MAX_LINE];

    msg.timestamp = 1704067200;
    snprintf(msg.username, sizeof(msg.username), "alice");
    snprintf(msg.content, sizeof(msg.content), "esc\033[2Jhere");
    assert(message_log_format_record(&msg, line, sizeof(line), NULL) < 0);

    snprintf(msg.content, sizeof(msg.content), "del\177here");
    assert(message_log_format_record(&msg, line, sizeof(line), NULL) < 0);
}

TEST(username_still_rejects_newline) {
    message_t msg = {0};
    char line[MESSAGE_LOG_MAX_LINE];

    msg.timestamp = 1704067200;
    snprintf(msg.username, sizeof(msg.username), "a\nb");
    snprintf(msg.content, sizeof(msg.content), "hi");
    assert(message_log_format_record(&msg, line, sizeof(line), NULL) < 0);
}

TEST(header_is_recognised_and_is_not_a_record) {
    message_t out = {0};
    assert(message_log_is_header(MESSAGE_LOG_HEADER "\n"));
    assert(!message_log_is_header("2024-01-01T00:00:00Z|a|b\n"));
    assert(!message_log_parse_record(MESSAGE_LOG_HEADER "\n", &out, time(NULL)));
}

TEST(a_v1_record_with_a_backslash_decodes_unchanged_after_migration) {
    /* Migration escapes the backslash, so the decoded text matches the
     * original.  This is the property that makes migration lossless. */
    char enc[128];
    char dec[128];
    assert(message_log_encode_content("C:\\new", enc, sizeof(enc)));
    assert(strcmp(enc, "C:\\\\new") == 0);
    assert(message_log_decode_content(enc, dec, sizeof(dec)));
    assert(strcmp(dec, "C:\\new") == 0);
}
```

- [ ] **Step 2: Run them, watch them fail, implement, run again**

Run: `cd tests/unit && make test_message_log && ./test_message_log`

- [ ] **Step 3: Register the new test with the gates and commit**

```bash
git commit -m "feat: escape newlines in the message log"
```

---

### Task 2: Migration

**Files:** `include/message_log.h`, `src/message_log.c`, `src/message.c`, `tests/unit/test_message_log.c`

**Interfaces:**
- `int message_log_migrate(const char *path)` — returns 0 when the file is already v2 or was migrated, -1 on failure.

Behaviour:
1. Missing file: nothing to do.
2. First line is the header: nothing to do. **This is what makes it idempotent.**
3. Otherwise: copy to `<path>.v1.bak`, write `<path>.tmp` containing the header followed by every original record with `\` escaped, `fsync`, `rename` over the original.

`message_save` writes the header when it creates the file, so a fresh install
is v2 from its first message and never migrates.

- [ ] **Step 1: Write the failing test**

```c
TEST(migration_escapes_backslashes_and_is_idempotent) {
    char dir[] = "/tmp/tnt-migrate-XXXXXX";
    char path[512];
    char backup[512];
    char first[512];
    char second[512];

    assert(mkdtemp(dir) != NULL);
    snprintf(path, sizeof(path), "%s/messages.log", dir);
    snprintf(backup, sizeof(backup), "%s/messages.log.v1.bak", dir);

    write_file(path, "2024-01-01T00:00:00Z|alice|C:\\new\n");

    assert(message_log_migrate(path) == 0);
    read_file(path, first, sizeof(first));
    assert(strncmp(first, MESSAGE_LOG_HEADER, strlen(MESSAGE_LOG_HEADER)) == 0);
    assert(strstr(first, "C:\\\\new") != NULL);
    assert(file_exists(backup));

    /* A second run must not escape the escape. */
    assert(message_log_migrate(path) == 0);
    read_file(path, second, sizeof(second));
    assert(strcmp(first, second) == 0);
}

TEST(migration_preserves_what_a_record_means) {
    /* The decoded content after migration equals the raw content before it. */
    message_t out = {0};
    char line[MESSAGE_LOG_MAX_LINE];
    snprintf(line, sizeof(line), "2024-01-01T00:00:00Z|alice|C:\\\\new\n");
    assert(message_log_parse_record(line, &out, 1704067200));
    assert(strcmp(out.content, "C:\\new") == 0);
}
```

- [ ] **Step 2: Implement, then call it at startup**

`message_load` runs the migration before reading, under the existing file lock.

- [ ] **Step 3: Commit**

```bash
git commit -m "feat: migrate an existing log to the escaped format"
```

---

### Task 3: The offline tools and an end-to-end migration test

**Files:** `src/main.c` (`--log-check`, `--log-recover`), `tests/test_log_migration.sh`, `Makefile`, `tnt-message-log.5`

- [ ] **Step 1: Teach the tools about the header**

`--log-check` accepts the header line rather than counting it as a bad record,
and reports whether the file is v1 or v2. `--log-recover` writes the header.

- [ ] **Step 2: Write the end-to-end test**

`tests/test_log_migration.sh`: seed a v1 log containing a backslash and a
plain record, start the server, and assert the backup exists, the header is
present, the backslash record survived with its meaning intact (via
`tntctl dump`), and a second start changes nothing.

- [ ] **Step 3: Documentation**

`tnt-message-log(5)` gains the encoding, the header, the migration, and the
rollback note: a 1.3.x binary reading a v2 log shows `\n` and `\\` literally
rather than dropping records.

- [ ] **Step 4: Gates, commit, pull request**

`make test`, `make release-check`.

---

## Sprint exit criteria

- A message containing a newline survives a write/read round trip, and every other control character is still refused.
- An existing log migrates once, keeps a backup, and a second start is a no-op.
- `tntctl dump` still emits one record per line.
- No user-visible behaviour change: nothing can produce a newline yet.
