# Approachable UI — Stage 2a: Cursor Editing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the message input a real cursor, so text can be edited anywhere in the line and pressing an arrow key never destroys the message.

**Architecture:** A pure `editor` module owns the buffer, the cursor, and every mutation, all on cluster boundaries. `tui_render_input` renders from that state and places the terminal cursor, scrolling horizontally to follow it. `input.c` keeps its current mode structure and every existing key keeps its current meaning; the new arrow, Home, End, and Delete keys stop falling through to the plain-ESC branch.

**Tech Stack:** C11, no new dependencies. Unit tests in the repository's `TEST`/`RUN_TEST` style.

**Spec:** `docs/superpowers/specs/2026-09-07-approachable-ui-design.md` (Stage 2, first half)

## Global Constraints

- C11, `-Wall -Wextra`, no new third-party dependencies.
- Per-connection resident cost must not exceed the current main. The editor replaces the session-local `char input[MAX_MESSAGE_LEN]`; it does not add a second buffer per client.
- **No existing key changes meaning in this stage.** `Ctrl+U` still clears the whole line, `Esc` still enters NORMAL, `:` still opens COMMAND. The non-modal default keymap is Stage 2b.
- No message-format change. Content stays single-line; `\n` arrives in Stage 3.
- New module ships `include/editor.h`, `src/editor.c`, `tests/unit/test_editor.c`, a `tests/unit/Makefile` entry, a `MAINTAINERS` entry, and a `.gitignore` line for the test binary.
- `make release-check` must pass before the final commit.
- Commit subjects are lowercase `type: subject`. Work lands through a pull request into `main`.

---

### Task 1: The `editor` module

**Files:**
- Create: `include/editor.h`, `src/editor.c`, `tests/unit/test_editor.c`
- Modify: `tests/unit/Makefile`, `MAINTAINERS`, `.gitignore`

**Interfaces:**
- Consumes: `utf8_cluster_length`, `utf8_cluster_width`, `utf8_string_width` from `utf8.h`.
- Produces:
  - `typedef struct { char buf[MAX_MESSAGE_LEN]; size_t len; size_t cursor; } editor_t;`
  - `void editor_reset(editor_t *ed)`
  - `bool editor_set_text(editor_t *ed, const char *text)` — replaces the buffer, cursor to end; false if `text` does not fit.
  - `const char *editor_text(const editor_t *ed)`, `size_t editor_len(const editor_t *ed)`, `size_t editor_cursor(const editor_t *ed)`
  - `int editor_cursor_column(const editor_t *ed)` — display columns before the cursor
  - `bool editor_insert_bytes(editor_t *ed, const char *bytes, size_t n)` — false on overflow, buffer unchanged
  - `bool editor_delete_prev_cluster(editor_t *ed)`, `bool editor_delete_next_cluster(editor_t *ed)`, `bool editor_delete_prev_word(editor_t *ed)`
  - `void editor_clear(editor_t *ed)`
  - `bool editor_move_left(editor_t *ed)`, `bool editor_move_right(editor_t *ed)`, `bool editor_move_prev_word(editor_t *ed)`, `bool editor_move_next_word(editor_t *ed)`, `void editor_move_home(editor_t *ed)`, `void editor_move_end(editor_t *ed)`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/test_editor.c` with the standard harness and these cases:

```c
TEST(editor_inserts_at_the_cursor) {
    editor_t ed;
    editor_reset(&ed);
    assert(editor_insert_bytes(&ed, "helo", 4));
    assert(editor_move_left(&ed));
    assert(editor_insert_bytes(&ed, "l", 1));
    assert(strcmp(editor_text(&ed), "hello") == 0);
    assert(editor_cursor(&ed) == 4);
}

TEST(editor_deletes_before_and_after_the_cursor) {
    editor_t ed;
    editor_reset(&ed);
    assert(editor_insert_bytes(&ed, "abcd", 4));
    assert(editor_move_left(&ed));
    assert(editor_delete_prev_cluster(&ed));   /* removes 'c' */
    assert(strcmp(editor_text(&ed), "abd") == 0);
    assert(editor_delete_next_cluster(&ed));   /* removes 'd' */
    assert(strcmp(editor_text(&ed), "ab") == 0);
}

TEST(editor_cursor_stays_on_cluster_boundaries) {
    editor_t ed;
    editor_reset(&ed);
    /* a + flag + b: moving left twice must land between 'a' and the flag,
     * never inside the flag's two regional indicators. */
    assert(editor_set_text(&ed, "a🇨🇳b"));
    assert(editor_move_left(&ed));
    assert(editor_move_left(&ed));
    assert(editor_cursor(&ed) == 1);
    assert(editor_cursor_column(&ed) == 1);
}

TEST(editor_backspace_removes_a_whole_emoji) {
    editor_t ed;
    editor_reset(&ed);
    assert(editor_set_text(&ed, "hi👨\xE2\x80\x8D👩"));
    assert(editor_delete_prev_cluster(&ed));
    assert(strcmp(editor_text(&ed), "hi") == 0);
}

TEST(editor_moves_stop_at_the_bounds) {
    editor_t ed;
    editor_reset(&ed);
    assert(editor_set_text(&ed, "ab"));
    editor_move_home(&ed);
    assert(!editor_move_left(&ed));
    assert(editor_cursor(&ed) == 0);
    editor_move_end(&ed);
    assert(!editor_move_right(&ed));
    assert(editor_cursor(&ed) == 2);
}

TEST(editor_word_operations) {
    editor_t ed;
    editor_reset(&ed);
    assert(editor_set_text(&ed, "one two three"));
    assert(editor_move_prev_word(&ed));
    assert(editor_cursor(&ed) == 8);
    assert(editor_delete_prev_word(&ed));
    assert(strcmp(editor_text(&ed), "one three") == 0);
}

TEST(editor_rejects_overflow_without_corrupting) {
    editor_t ed;
    char big[MAX_MESSAGE_LEN];
    editor_reset(&ed);
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    assert(editor_insert_bytes(&ed, big, strlen(big)) == false);
    assert(editor_len(&ed) == 0);
    assert(strcmp(editor_text(&ed), "") == 0);
}

TEST(editor_cursor_column_counts_display_width) {
    editor_t ed;
    editor_reset(&ed);
    assert(editor_set_text(&ed, "中文a"));
    editor_move_home(&ed);
    assert(editor_cursor_column(&ed) == 0);
    assert(editor_move_right(&ed));
    assert(editor_cursor_column(&ed) == 2);
}
```

- [ ] **Step 2: Wire the test into the build and run it**

Add `EDITOR_SRC = ../../src/editor.c`, append `test_editor` to `TESTS`, add

```make
test_editor: test_editor.c $(EDITOR_SRC) $(UTF8_SRC)
	$(CC) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(PROJECT_LDLIBS) $(LDLIBS)
```

and a `run` recipe entry. Then:

Run: `cd tests/unit && make test_editor`
Expected: FAIL — `include/editor.h` does not exist.

- [ ] **Step 3: Write the header**

`include/editor.h` declares the struct and the functions listed under
Interfaces above, including `common.h` for `MAX_MESSAGE_LEN` and `stdbool.h`.

- [ ] **Step 4: Implement the module**

`src/editor.c`. The one non-obvious helper is backward cluster movement,
since `utf8_cluster_length` only walks forward:

```c
/* Byte offset of the cluster boundary immediately before `pos`.  Walking
 * from the start is O(n) with n <= MAX_MESSAGE_LEN, which is far cheaper
 * than maintaining a boundary index for a single input line. */
static size_t cluster_start_before(const editor_t *ed, size_t pos) {
    size_t offset = 0;
    size_t prev = 0;

    while (offset < pos) {
        size_t len = utf8_cluster_length(ed->buf + offset);

        if (len == 0) {
            break;
        }
        prev = offset;
        offset += len;
    }
    return prev;
}
```

Insertion shifts the tail with `memmove` and advances the cursor; deletion
shifts it back. Every mutation keeps `buf[len] == '\0'` and leaves
`cursor <= len` on a cluster boundary.

- [ ] **Step 5: Run the tests**

Run: `cd tests/unit && make test_editor && ./test_editor`
Expected: PASS, eight cases, zero warnings.

- [ ] **Step 6: Register with the gates and commit**

Add `F: include/editor.h`, `F: src/editor.c`, `F: tests/unit/test_editor.c` to
the `INTERACTIVE TUI` section of `MAINTAINERS`, and `tests/unit/test_editor`
to `.gitignore`.

Run: `scripts/check_maintainers.sh` → `check-maintainers: ok`

```bash
git add include/editor.h src/editor.c tests/unit/test_editor.c \
        tests/unit/Makefile MAINTAINERS .gitignore
git commit -m "feat: add a cursor-aware input editor"
```

---

### Task 2: Render the input line from the editor

**Files:**
- Modify: `include/tui.h`, `src/tui.c` (`tui_render_input`)

**Interfaces:**
- Consumes: `editor_t` and its accessors from Task 1.
- Produces: `void tui_render_input(client_t *client, const editor_t *ed)` — the
  signature changes from `const char *input` to the editor, because the
  renderer now needs the cursor.

- [ ] **Step 1: Follow the cursor when scrolling horizontally**

The current horizontal scroll keeps the *end* of the text visible. It must
keep the *cursor* visible instead, or editing at the start of a long line
scrolls the caret off screen. Replace the `excess`/`skip_width` block with a
window whose start is the last cluster boundary such that
`cursor_column - start_column <= avail - 1`.

- [ ] **Step 2: Place the terminal cursor**

The input row begins with `›` plus a space, so text starts at column 3.
After emitting the line, append a cursor-positioning escape:

```c
    int cursor_col = 3 + editor_cursor_column(ed) - scroll_columns;
    if (cursor_col < 3) cursor_col = 3;
    buffer_appendf(buffer, sizeof(buffer), &pos, "\033[%d;%dH", rh,
                   cursor_col);
```

- [ ] **Step 3: Build and eyeball it**

Run: `make` then start a server and connect with `expect`, typing a line and
pressing Left twice; confirm the caret sits two columns back and that typing
inserts there.

- [ ] **Step 4: Commit**

```bash
git add include/tui.h src/tui.c
git commit -m "feat: draw the input caret at the editing position"
```

---

### Task 3: Drive the editor from the input loop

**Files:**
- Modify: `src/input.c` — `handle_key` INSERT branch, the paste path, the
  history-recall path, and the printable-append path in `input_run_session`

**Interfaces:**
- Consumes: Tasks 1 and 2.
- Produces: no new public interface. `handle_key`'s `char *input, size_t *input_len` parameters become `editor_t *ed`.

- [ ] **Step 1: Write the failing integration test**

Create `tests/test_cursor_editing.sh` following `tests/test_interactive_input.sh`,
launching the server with `TNT_MAX_CONN_PER_IP=256 TNT_MAX_CONNECTIONS=256` and
a 15-iteration readiness poll. Three assertions:

1. type `helo world`, press Left five times, type `l`, press Enter — the log
   must contain `hello world`
2. type `abc`, press Home, type `X`, press Enter — the log must contain `Xabc`
3. type `abc`, press Left, press Delete (`ESC [ 3 ~`), press Enter — the log
   must contain `ab`

Assertion 1 is the reproduction from the design document; before this task it
fails because the message is discarded.

Run: `cd tests && PORT=13780 sh test_cursor_editing.sh`
Expected: FAIL on all three.

- [ ] **Step 2: Replace the INSERT-mode escape handling**

In `handle_key`'s `MODE_INSERT` branch, the `ESC [` dispatch currently knows
only `A`, `B`, and `200~`. Extend it so every recognised sequence is handled
and **unrecognised sequences are swallowed rather than falling through to the
NORMAL-mode switch**:

```c
                    if (n == 1) {
                        switch (seq[1]) {
                        case 'A': /* Up: history back */
                        case 'B': /* Down: history forward */
                            /* unchanged history-recall bodies */
                            return true;
                        case 'C':
                            editor_move_right(ed);
                            tui_render_input(client, ed);
                            return true;
                        case 'D':
                            editor_move_left(ed);
                            tui_render_input(client, ed);
                            return true;
                        case 'H':
                            editor_move_home(ed);
                            tui_render_input(client, ed);
                            return true;
                        case 'F':
                            editor_move_end(ed);
                            tui_render_input(client, ed);
                            return true;
                        case '1': case '3': case '4': case '2':
                            /* "ESC [ <n> ~" family: Home, Delete, End, and
                             * the bracketed-paste introducer. */
                            return handle_insert_csi_tilde(client, ed, seq[1]);
                        default:
                            /* An escape sequence this build does not know.
                             * Discard it: never let it mean "leave INSERT". */
                            return true;
                        }
                    }
```

`handle_insert_csi_tilde` reads the remaining bytes, dispatches `1~`/`7~` to
Home, `3~` to `editor_delete_next_cluster`, `4~`/`8~` to End, and `200~` to
the existing paste path.

- [ ] **Step 3: Convert the remaining INSERT keys**

`Backspace` becomes `editor_delete_prev_cluster`, `Ctrl+W` becomes
`editor_delete_prev_word`, `Ctrl+U` keeps its current meaning through
`editor_clear`, `Enter` sends `editor_text(ed)`, and the `@`-completion path
operates on `editor_text` and finishes with `editor_set_text`. The printable
append in `input_run_session` calls `editor_insert_bytes`.

- [ ] **Step 4: Run the integration test**

Run: `cd tests && PORT=13781 sh test_cursor_editing.sh`
Expected: PASS on all three.

- [ ] **Step 5: Prove the old behaviour is gone**

Run the reproduction from the design document by hand: type, press Left,
press Enter, then check the server's `messages.log`.
Expected: the message is present. Before this task it was absent.

- [ ] **Step 6: Full suite**

Run: `make test PORT=13790`
Expected: exit 0, no failures. `test_interactive_input.sh` in particular must
still pass unchanged — it covers the keys this task rewires.

- [ ] **Step 7: Commit**

```bash
git add src/input.c tests/test_cursor_editing.sh Makefile
git commit -m "fix: edit the message line at the cursor"
```

---

### Task 4: Documentation and stage close

**Files:**
- Modify: `tnt-chat.7`, `Makefile` (register the new test)

- [ ] **Step 1: Document the keys**

In the INSERT mode section of `tnt-chat.7`, add `Left`, `Right`, `Home`,
`End`, and `Delete`, and state that an unrecognised escape sequence is
ignored rather than changing modes. Keep lines within 80 columns.

Run: `sh tests/test_manpages.sh` → 14 passed.

- [ ] **Step 2: Release gate**

Run: `make release-check` → `release preflight passed`.

- [ ] **Step 3: Commit and open the pull request**

```bash
git add tnt-chat.7 Makefile
git commit -m "docs: describe cursor editing keys"
git push -u origin feat/cursor-editing
gh pr create --base main --title "Edit the message line at the cursor" \
  --body "Stage 2a of docs/superpowers/specs/2026-09-07-approachable-ui-design.md"
```

---

## Sprint exit criteria

- Typing, pressing Left, and pressing Enter sends the message. The reported data-loss bug is gone and covered by a regression test.
- Text can be inserted and deleted anywhere in the line, and the caret is visible at the editing position.
- No existing key changed meaning; `test_interactive_input.sh` passes unchanged.
- `make test` and `make release-check` are green.
- Verified on the live instance.
