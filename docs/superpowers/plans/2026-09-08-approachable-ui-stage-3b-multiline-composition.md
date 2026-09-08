# Approachable UI — Stage 3b: Multi-line Composition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a user compose, paste, send, and read a message that contains real line breaks.

**Architecture:** Stage 3a already made the stored format carry a newline, and Stage 1 already made `richtext_wrap` treat `\n` as a hard break, so the history region needs no change. What is left is the composing half: the `editor` buffer admits `\n` and gains display-line navigation by reusing `richtext_wrap`; `tui_render_input` draws an input region of 1 to 6 rows and the history region shrinks by the difference; the paste path and `message_save` stop flattening newlines.

**Tech Stack:** C11, no new dependencies.

**Spec:** `docs/superpowers/specs/2026-09-07-approachable-ui-design.md` (Stage 3, composition half)

## Deliberate deviations from the spec

Decided 2026-09-08. The spec is a dated design record and is not being edited;
these are the differences an executor must follow.

1. **No `Shift+Enter`.** The spec's preferred layer needs kitty-keyboard /
   `modifyOtherKeys` negotiation, which adds a terminal-state handshake with a
   restore-on-exit failure mode. Ship `Ctrl+J` and `Alt+Enter` only, both of
   which need no negotiation. `Shift+Enter` can be a later PR on its own.
2. **No `o` / `O` in vim NORMAL.** vim mode gets multi-line through `Ctrl+J` in
   INSERT and nothing else, so the NORMAL key table is untouched and the vim
   regression suite carries zero risk.
3. **No intent layer, no `struct client` change.** The spec's byte-to-intent
   table was motivated by the Left-arrow data-loss bug, which Stage 2a already
   closed. `editor_t` stays a local in `input_run_session()` and
   `insert_history[16][MAX_MESSAGE_LEN]` is not reshaped. Nothing in this stage
   needs either.

## Global Constraints

- C11, `-Wall -Wextra`, no new third-party dependencies.
- **Only `\n` is admitted into content.** Every other C0 and C1 control
  character and DEL stays rejected, for client messages and module messages
  alike. Usernames still reject all control characters including `\n`.
- The 1023-byte content limit applies to the **encoded** form (`\` and newline
  each cost two bytes), so the length gauge and the send guard both count
  encoded length.
- `tntctl dump` and `tail` keep emitting the escaped, one-record-per-line form.
- `exec post` stays single-line.
- The input region is at most 6 display rows; the existing `height < 4` clamp
  keeps small terminals safe.
- Adding a module means six files: `include/x.h`, `src/x.c`,
  `tests/unit/test_x.c`, a `TESTS` entry + build rule + `run` recipe line in
  `tests/unit/Makefile`, a `MAINTAINERS` entry, and a `.gitignore` line for the
  test binary. This stage adds no module, but new test files still need the
  Makefile, MAINTAINERS, and `.gitignore` entries.
- Any new server-launching test sets `TNT_MAX_CONN_PER_IP=256
  TNT_MAX_CONNECTIONS=256` and polls readiness 15 times, and declares its
  keymap (`TNT_KEYMAP=vim` for modal suites) rather than relying on the build
  default.
- `make test` and `make release-check` green; lands through a pull request.

---

### Task 1: The editor admits a newline and navigates display lines

**Files:**
- Modify: `include/editor.h`
- Modify: `src/editor.c`
- Modify: `tests/unit/test_editor.c`
- Modify: `tests/unit/Makefile:62-63` (link `$(RICHTEXT_SRC)` into `test_editor`)

**Interfaces:**
- Consumes: `richtext_wrap(const char *text, int width, richtext_span_t *out, size_t max_out)` from `include/richtext.h`, which already treats `\n` as a hard break.
- Produces, all used by Tasks 4 and 5:
  - `#define EDITOR_MAX_ROWS 6`
  - `bool editor_insert_newline(editor_t *ed)`
  - `int editor_display_rows(const editor_t *ed, int width)` — total display rows, at least 1
  - `int editor_caret_row(const editor_t *ed, int width)` — 0-based display row holding the caret
  - `int editor_caret_column(const editor_t *ed, int width)` — display columns before the caret **within its own row**
  - `bool editor_move_up(editor_t *ed, int width)`
  - `bool editor_move_down(editor_t *ed, int width)`
  - `void editor_move_home(editor_t *ed, int width)` — start of the caret's display row (signature change)
  - `void editor_move_end(editor_t *ed, int width)` — end of the caret's display row (signature change)

`editor_cursor_column(ed)` keeps its current meaning (columns from the start of
the whole buffer) and its current signature; Task 4 stops using it.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/test_editor.c`, and add a `RUN_TEST` line for each in `main()`:

```c
TEST(editor_inserts_a_newline) {
    editor_t ed;

    editor_reset(&ed);
    assert(editor_insert_bytes(&ed, "ab", 2));
    assert(editor_insert_newline(&ed));
    assert(editor_insert_bytes(&ed, "cd", 2));
    assert(strcmp(editor_text(&ed), "ab\ncd") == 0);
    assert(editor_cursor(&ed) == 5);
}

TEST(editor_counts_display_rows) {
    editor_t ed;

    editor_reset(&ed);
    assert(editor_set_text(&ed, ""));
    assert(editor_display_rows(&ed, 20) == 1);   /* empty is still one row */

    assert(editor_set_text(&ed, "one\ntwo\nthree"));
    assert(editor_display_rows(&ed, 20) == 3);   /* hard breaks */

    assert(editor_set_text(&ed, "aaaaaaaaaa"));
    assert(editor_display_rows(&ed, 4) == 3);    /* soft wrap: 4+4+2 */
}

TEST(editor_reports_the_caret_row_and_column) {
    editor_t ed;

    editor_reset(&ed);
    assert(editor_set_text(&ed, "one\ntwo"));    /* cursor at the end */
    assert(editor_caret_row(&ed, 20) == 1);
    assert(editor_caret_column(&ed, 20) == 3);

    editor_move_home(&ed, 20);
    assert(editor_caret_row(&ed, 20) == 1);
    assert(editor_caret_column(&ed, 20) == 0);
    assert(editor_cursor(&ed) == 4);             /* after the newline */
}

TEST(editor_moves_between_display_lines) {
    editor_t ed;

    editor_reset(&ed);
    assert(editor_set_text(&ed, "abcd\nefgh"));  /* cursor at the end */
    assert(editor_move_up(&ed, 20));
    assert(editor_caret_row(&ed, 20) == 0);
    assert(editor_caret_column(&ed, 20) == 4);   /* column is preserved */

    assert(!editor_move_up(&ed, 20));            /* already on the first row */

    assert(editor_move_down(&ed, 20));
    assert(editor_caret_row(&ed, 20) == 1);
    assert(!editor_move_down(&ed, 20));          /* already on the last row */
}

TEST(editor_up_clamps_to_a_shorter_row) {
    editor_t ed;

    editor_reset(&ed);
    assert(editor_set_text(&ed, "ab\nefgh"));    /* cursor at the end, col 4 */
    assert(editor_move_up(&ed, 20));
    assert(editor_caret_row(&ed, 20) == 0);
    assert(editor_caret_column(&ed, 20) == 2);   /* clamped to the row's end */
    assert(editor_cursor(&ed) == 2);
}

TEST(editor_home_and_end_are_scoped_to_the_display_row) {
    editor_t ed;

    editor_reset(&ed);
    assert(editor_set_text(&ed, "one\ntwo"));
    editor_move_home(&ed, 20);
    assert(editor_cursor(&ed) == 4);
    editor_move_end(&ed, 20);
    assert(editor_cursor(&ed) == 7);

    assert(editor_move_up(&ed, 20));
    editor_move_end(&ed, 20);
    assert(editor_cursor(&ed) == 3);             /* end of "one", not of the buffer */
}

TEST(editor_navigation_keeps_cluster_boundaries) {
    editor_t ed;

    editor_reset(&ed);
    assert(editor_set_text(&ed, "😌😌\nab"));
    assert(editor_move_up(&ed, 20));
    /* The caret must land on a cluster start, never inside the emoji. */
    assert(editor_cursor(&ed) == 0 || editor_cursor(&ed) == 4 ||
           editor_cursor(&ed) == 8);
}

TEST(editor_rows_are_bounded_by_the_wrap_table) {
    editor_t ed;
    char many[MAX_MESSAGE_LEN];
    size_t i;

    editor_reset(&ed);
    for (i = 0; i + 1 < sizeof(many); i++) {
        many[i] = (i % 2) ? '\n' : 'x';
    }
    many[sizeof(many) - 1] = '\0';
    assert(editor_set_text(&ed, many));
    /* More logical lines than the wrap table holds must not overrun it. */
    assert(editor_display_rows(&ed, 20) >= 1);
    assert(editor_display_rows(&ed, 20) <= EDITOR_ROW_TABLE);
}
```

- [ ] **Step 2: Run them and watch them fail**

Run: `cd tests/unit && make test_editor`
Expected: FAIL to compile — `editor_insert_newline`, `editor_display_rows`,
`editor_caret_row`, `editor_caret_column`, `editor_move_up`,
`editor_move_down`, `EDITOR_ROW_TABLE` are undeclared, and `editor_move_home`
/ `editor_move_end` take one argument.

- [ ] **Step 3: Extend the header**

In `include/editor.h`, add the include and the declarations:

```c
#include "richtext.h"

/* Display rows the input region may occupy before it scrolls internally. */
#define EDITOR_MAX_ROWS 6

/* Upper bound on the wrap table.  A message is at most MAX_MESSAGE_LEN bytes,
 * so it cannot produce more display rows than that, but the editor only ever
 * needs enough rows to place the caret and size the region. */
#define EDITOR_ROW_TABLE 128

bool editor_insert_newline(editor_t *ed);

/* Display-line geometry at the given render width.  Width < 1 is treated as
 * 1.  Rows are always at least 1, including for an empty buffer. */
int editor_display_rows(const editor_t *ed, int width);
int editor_caret_row(const editor_t *ed, int width);
int editor_caret_column(const editor_t *ed, int width);

bool editor_move_up(editor_t *ed, int width);
bool editor_move_down(editor_t *ed, int width);
```

Change the two existing declarations to take a width:

```c
void editor_move_home(editor_t *ed, int width);
void editor_move_end(editor_t *ed, int width);
```

Update the struct comment, which currently says "A single-line message
buffer": it is now "A message buffer with a cursor, which may contain
newlines."

- [ ] **Step 4: Implement**

In `src/editor.c`, add `#include "richtext.h"` and this block above
`editor_move_home`:

```c
/* One shared layout pass.  Rows are byte ranges into the buffer; a hard
 * newline and a soft wrap both end a row, which is exactly what the user
 * sees. */
static size_t layout(const editor_t *ed, int width, richtext_span_t *out) {
    size_t rows;

    if (width < 1) {
        width = 1;
    }
    rows = richtext_wrap(ed->buf, width, out, EDITOR_ROW_TABLE);
    if (rows == 0) {
        out[0].offset = 0;
        out[0].len = 0;
        rows = 1;
    }
    return rows;
}

/* Index of the row holding the caret.  A caret sitting exactly on a row
 * boundary belongs to the earlier row unless that row ended at a newline,
 * which is what puts the caret on the new line after Ctrl+J. */
static size_t caret_row_index(const editor_t *ed, const richtext_span_t *rows,
                              size_t row_count) {
    size_t i;

    for (i = 0; i < row_count; i++) {
        size_t end = rows[i].offset + rows[i].len;

        if (ed->cursor < end) {
            return i;
        }
        if (ed->cursor == end && i + 1 == row_count) {
            return i;
        }
    }
    return row_count - 1;
}

static int columns_between(const editor_t *ed, size_t from, size_t to) {
    size_t offset = from;
    int width = 0;

    while (offset < to) {
        size_t len = utf8_cluster_length(ed->buf + offset);

        if (len == 0) {
            break;
        }
        width += utf8_cluster_width(ed->buf + offset);
        offset += len;
    }
    return width;
}

/* Byte offset within `row` that sits at or before `column` display columns,
 * always on a cluster boundary. */
static size_t offset_for_column(const editor_t *ed,
                                const richtext_span_t *row, int column) {
    size_t offset = row->offset;
    size_t end = row->offset + row->len;
    int width = 0;

    while (offset < end) {
        size_t len = utf8_cluster_length(ed->buf + offset);
        int w;

        if (len == 0) {
            break;
        }
        w = utf8_cluster_width(ed->buf + offset);
        if (width + w > column) {
            break;
        }
        width += w;
        offset += len;
    }
    return offset;
}

bool editor_insert_newline(editor_t *ed) {
    return editor_insert_bytes(ed, "\n", 1);
}

int editor_display_rows(const editor_t *ed, int width) {
    richtext_span_t rows[EDITOR_ROW_TABLE];

    if (!ed) {
        return 1;
    }
    return (int)layout(ed, width, rows);
}

int editor_caret_row(const editor_t *ed, int width) {
    richtext_span_t rows[EDITOR_ROW_TABLE];
    size_t count;

    if (!ed) {
        return 0;
    }
    count = layout(ed, width, rows);
    return (int)caret_row_index(ed, rows, count);
}

int editor_caret_column(const editor_t *ed, int width) {
    richtext_span_t rows[EDITOR_ROW_TABLE];
    size_t count;
    size_t index;

    if (!ed) {
        return 0;
    }
    count = layout(ed, width, rows);
    index = caret_row_index(ed, rows, count);
    return columns_between(ed, rows[index].offset, ed->cursor);
}

bool editor_move_up(editor_t *ed, int width) {
    richtext_span_t rows[EDITOR_ROW_TABLE];
    size_t count;
    size_t index;
    int column;

    if (!ed) {
        return false;
    }
    count = layout(ed, width, rows);
    index = caret_row_index(ed, rows, count);
    if (index == 0) {
        return false;
    }
    column = columns_between(ed, rows[index].offset, ed->cursor);
    ed->cursor = offset_for_column(ed, &rows[index - 1], column);
    return true;
}

bool editor_move_down(editor_t *ed, int width) {
    richtext_span_t rows[EDITOR_ROW_TABLE];
    size_t count;
    size_t index;
    int column;

    if (!ed) {
        return false;
    }
    count = layout(ed, width, rows);
    index = caret_row_index(ed, rows, count);
    if (index + 1 >= count) {
        return false;
    }
    column = columns_between(ed, rows[index].offset, ed->cursor);
    ed->cursor = offset_for_column(ed, &rows[index + 1], column);
    return true;
}
```

Replace the two existing movers:

```c
void editor_move_home(editor_t *ed, int width) {
    richtext_span_t rows[EDITOR_ROW_TABLE];
    size_t count;

    if (!ed) {
        return;
    }
    count = layout(ed, width, rows);
    ed->cursor = rows[caret_row_index(ed, rows, count)].offset;
}

void editor_move_end(editor_t *ed, int width) {
    richtext_span_t rows[EDITOR_ROW_TABLE];
    size_t count;
    size_t index;

    if (!ed) {
        return;
    }
    count = layout(ed, width, rows);
    index = caret_row_index(ed, rows, count);
    ed->cursor = rows[index].offset + rows[index].len;
}
```

- [ ] **Step 5: Link richtext into the editor unit test**

In `tests/unit/Makefile`, change the `test_editor` rule to include the
richtext source:

```make
test_editor: test_editor.c $(EDITOR_SRC) $(RICHTEXT_SRC) $(UTF8_SRC)
	$(CC) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(PROJECT_LDLIBS) $(LDLIBS)
```

- [ ] **Step 6: Run the tests**

Run: `cd tests/unit && make test_editor && ./test_editor`
Expected: PASS, all editor tests.

The two signature changes break `src/input.c`, which still calls
`editor_move_home(ed)` and `editor_move_end(ed)` at
`src/input.c:597` and `:604` and around `:723`/`:727`. Fix those call sites
now by passing the input render width, which Task 4 defines as
`client->width - 3`; until then use `client->width - 3` inline so the tree
builds. Run `make` at the repository root and confirm it links.

- [ ] **Step 7: Commit**

```bash
git add include/editor.h src/editor.c src/input.c tests/unit/test_editor.c tests/unit/Makefile
git commit -m "feat: give the editor newlines and display-line navigation"
```

---

### Task 2: Paste keeps its line breaks

**Files:**
- Modify: `src/input_buffer.c:138`
- Modify: `tests/unit/test_input_buffer.c`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: no new symbols. `tnt_input_append_stream_byte()` keeps its
  signature and changes behaviour only when `paste_mode` is true.

Today `src/input_buffer.c:138` maps `\r`, `\n`, and `\t` to a space during a
bracketed paste. A newline must now survive; `\r` becomes `\n` so a CRLF
paste yields one break rather than two, and a lone `\r` still breaks the line;
`\t` stays a space because a tab is not admitted into content.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/test_input_buffer.c`, and add a `RUN_TEST` line for each
in `main()`:

```c
TEST(paste_keeps_newlines) {
    char input[64] = "";
    size_t len = 0;
    tnt_input_utf8_state_t state;
    const char *paste = "one\ntwo";
    size_t i;

    tnt_input_utf8_state_reset(&state);
    for (i = 0; paste[i]; i++) {
        tnt_input_append_stream_byte(input, sizeof(input), &len, &state,
                                     (unsigned char)paste[i], true);
    }
    assert(strcmp(input, "one\ntwo") == 0);
}

TEST(paste_normalises_crlf_and_flattens_tabs) {
    char input[64] = "";
    size_t len = 0;
    tnt_input_utf8_state_t state;
    const char *paste = "a\r\nb\rc\td";
    size_t i;

    tnt_input_utf8_state_reset(&state);
    for (i = 0; paste[i]; i++) {
        tnt_input_append_stream_byte(input, sizeof(input), &len, &state,
                                     (unsigned char)paste[i], true);
    }
    assert(strcmp(input, "a\nb\nc d") == 0);
}

TEST(typed_control_bytes_are_still_ignored) {
    char input[64] = "";
    size_t len = 0;
    tnt_input_utf8_state_t state;

    tnt_input_utf8_state_reset(&state);
    /* paste_mode false: a newline is a key, not content. */
    tnt_input_append_stream_byte(input, sizeof(input), &len, &state,
                                 (unsigned char)'\n', false);
    assert(strcmp(input, "") == 0);
}
```

- [ ] **Step 2: Run them and watch them fail**

Run: `cd tests/unit && make test_input_buffer && ./test_input_buffer`
Expected: FAIL — `paste_keeps_newlines` gets `"one two"`.

- [ ] **Step 3: Implement**

Replace `src/input_buffer.c:138-140`:

```c
    if (paste_mode) {
        if (b == '\r') {
            /* CRLF becomes one break: the LF that follows is swallowed by the
             * newline already appended for this CR. */
            b = '\n';
        } else if (b == '\t') {
            b = ' ';
        }
        if (b == '\n') {
            if (*input_len > 0 && input[*input_len - 1] == '\n' &&
                state->crlf_pending) {
                state->crlf_pending = false;
                return status;
            }
            state->crlf_pending = true;
            if (state->len > 0) {
                tnt_input_utf8_state_reset(state);
                status |= TNT_INPUT_APPEND_INVALID_UTF8;
            }
            if (*input_len + 1 >= input_size) {
                return status | TNT_INPUT_APPEND_OVERFLOW;
            }
            input[(*input_len)++] = '\n';
            input[*input_len] = '\0';
            return status;
        }
        state->crlf_pending = false;
    }
```

Add `bool crlf_pending;` to `tnt_input_utf8_state_t` in
`include/input_buffer.h` and clear it in `tnt_input_utf8_state_reset()`.

- [ ] **Step 4: Run the tests**

Run: `cd tests/unit && make test_input_buffer && ./test_input_buffer`
Expected: PASS, all input buffer tests.

- [ ] **Step 5: Commit**

```bash
git add include/input_buffer.h src/input_buffer.c tests/unit/test_input_buffer.c
git commit -m "feat: keep line breaks when pasting"
```

---

### Task 3: Storage stops flattening newlines

**Files:**
- Modify: `src/message.c` (the content sanitising loop in `message_save`)
- Modify: `include/message_log.h`, `src/message_log.c` (add `message_log_encoded_length`)
- Modify: `tests/unit/test_message.c`
- Modify: `tests/unit/test_message_log.c`

**Interfaces:**
- Consumes: `message_log_encode_content()` from Stage 3a.
- Produces, used by Tasks 4 and 5:
  - `size_t message_log_encoded_length(const char *in)` — bytes the content
    would occupy escaped, excluding the terminator.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/test_message_log.c`, with a `RUN_TEST` line in `main()`:

```c
TEST(encoded_length_counts_the_escapes) {
    assert(message_log_encoded_length("plain") == 5);
    assert(message_log_encoded_length("a\\b") == 4);
    assert(message_log_encoded_length("one\ntwo") == 8);
    assert(message_log_encoded_length("") == 0);
    assert(message_log_encoded_length(NULL) == 0);
}
```

Append to `tests/unit/test_message.c`, with a `RUN_TEST` line in `main()`:

```c
TEST(message_save_keeps_a_newline) {
    message_t msg = { .timestamp = time(NULL) };
    message_t *messages = NULL;

    setup_state_dir();
    strcpy(msg.username, "alice");
    strcpy(msg.content, "first\nsecond");

    assert(message_save(&msg) == 0);
    assert(message_load(&messages, 10) == 1);
    assert(strcmp(messages[0].content, "first\nsecond") == 0);
    free(messages);
    cleanup_state_dir();
}

TEST(message_save_still_flattens_a_carriage_return) {
    message_t msg = { .timestamp = time(NULL) };
    message_t *messages = NULL;

    setup_state_dir();
    strcpy(msg.username, "alice");
    strcpy(msg.content, "first\rsecond");

    assert(message_save(&msg) == 0);
    assert(message_load(&messages, 10) == 1);
    assert(strcmp(messages[0].content, "first second") == 0);
    free(messages);
    cleanup_state_dir();
}
```

- [ ] **Step 2: Run them and watch them fail**

Run: `cd tests/unit && make test_message_log test_message && ./test_message_log && ./test_message`
Expected: FAIL — `message_log_encoded_length` undeclared, and
`message_save_keeps_a_newline` gets `"first second"`.

- [ ] **Step 3: Implement**

In `include/message_log.h`, beside the encode declaration:

```c
/* Bytes the content occupies once escaped, excluding the terminator.  The
 * 1023-byte field limit applies to this number, not to the decoded text. */
size_t message_log_encoded_length(const char *in);
```

In `src/message_log.c`:

```c
size_t message_log_encoded_length(const char *in) {
    size_t n = 0;

    if (!in) {
        return 0;
    }
    for (; *in; in++) {
        n += (*in == '\\' || *in == '\n') ? 2 : 1;
    }
    return n;
}
```

In `src/message.c`, the content loop in `message_save` currently reads:

```c
    for (char *p = safe_msg.content; *p; p++) {
        if (*p == '|' || *p == '\n' || *p == '\r') {
            *p = ' ';
        }
    }
```

Replace it with:

```c
    /* A newline is content now; the record layer escapes it.  The separator
     * and a carriage return are still flattened, the first because it would
     * break the record and the second because only `\n` is admitted. */
    for (char *p = safe_msg.content; *p; p++) {
        if (*p == '|' || *p == '\r') {
            *p = ' ';
        }
    }
```

The username loop above it is unchanged and still flattens `\n`.

**`src/exec.c` needs no change.** `exec_command_post` already rejects a
newline at `src/exec.c:403` through `utf8_contains_control(content)`, which
admits no C0 character at all — the newline exception lives only in
`message_log.c`'s `content_has_forbidden_control`. Verify this rather than
assuming it, with a step in Task 6's end-to-end test.

- [ ] **Step 4: Run the tests**

Run: `cd tests/unit && make run`
Expected: PASS, all suites.

- [ ] **Step 5: Commit**

```bash
git add include/message_log.h src/message_log.c src/message.c tests/unit/test_message.c tests/unit/test_message_log.c
git commit -m "feat: store a composed newline instead of flattening it"
```

---

### Task 4: The input region grows to N rows

**Files:**
- Modify: `include/history_view.h`, `src/history_view.c` (`history_view_height` takes the input row count)
- Modify: `src/tui.c` (`tui_render_input`, and the `history_view_height` call at `:299`)
- Modify: `src/input.c` (the `history_view_height` calls at `:279`, `:286`, `:744`, `:1051`)
- Modify: `tests/unit/test_history_view.c:26-29`

**Interfaces:**
- Consumes: `editor_display_rows()`, `editor_caret_row()`,
  `editor_caret_column()`, `EDITOR_MAX_ROWS` from Task 1;
  `message_log_encoded_length()` from Task 3.
- Produces, used by Task 5:
  - `int history_view_height(int terminal_height, int input_rows)`
  - `int tui_input_rows(const client_t *client, const editor_t *ed)` — the
    input region's current height, 1 to `EDITOR_MAX_ROWS`

The input region is drawn at the bottom of the screen. With one row it
occupies row `rh` exactly as today; with `n` rows it occupies rows
`rh - n + 1` through `rh`. The history region loses the extra `n - 1` rows.

- [ ] **Step 1: Write the failing test**

Replace `tests/unit/test_history_view.c:26-29` with:

```c
    assert(history_view_height(24, 1) == 21);
    assert(history_view_height(24, 3) == 19);
    assert(history_view_height(24, 6) == 16);
    assert(history_view_height(4, 1) == 1);
    assert(history_view_height(4, 6) == 1);   /* never below one row */
    assert(history_view_height(1, 1) == 1);
    assert(history_view_height(0, 1) == 1);
```

- [ ] **Step 2: Run it and watch it fail**

Run: `cd tests/unit && make test_history_view`
Expected: FAIL to compile — `history_view_height` takes one argument.

- [ ] **Step 3: Implement the height split**

In `include/history_view.h`:

```c
/* Rows left for the history region once the input region takes `input_rows`
 * of the bottom of the screen.  Always at least 1. */
int history_view_height(int terminal_height, int input_rows);
```

In `src/history_view.c`:

```c
int history_view_height(int terminal_height, int input_rows) {
    int height;

    if (input_rows < 1) {
        input_rows = 1;
    }
    height = terminal_height - 3 - (input_rows - 1);
    return height < 1 ? 1 : height;
}
```

- [ ] **Step 4: Run the test**

Run: `cd tests/unit && make test_history_view && ./test_history_view`
Expected: PASS.

- [ ] **Step 5: Render N rows**

In `src/tui.c`, add above `tui_render_input`:

```c
/* Columns the input text may use: the whole width less the "› " prompt and
 * one trailing column, so a caret parked after the last cluster still has
 * somewhere to sit.  Exported because input.c must move the caret with the
 * same width tui.c renders with, or Home lands on the wrong row. */
int tui_input_content_width(const client_t *client) {
    int rw = client ? client->width : 0;

    if (rw < 10) rw = 10;
    return rw - 3;
}

int tui_input_rows(const client_t *client, const editor_t *ed) {
    int rows;

    if (!client || !ed) {
        return 1;
    }
    rows = editor_display_rows(ed, tui_input_content_width(client));
    if (rows < 1) rows = 1;
    if (rows > EDITOR_MAX_ROWS) rows = EDITOR_MAX_ROWS;
    return rows;
}
```

Declare both in `include/tui.h`:

```c
int tui_input_content_width(const client_t *client);
int tui_input_rows(const client_t *client, const editor_t *ed);
```

Rewrite the body of `tui_render_input` so it lays the text out once and emits
one screen row per visible display row. The gauge stays on the **last** row.
The parts that change:

```c
void tui_render_input(client_t *client, const editor_t *ed) {
    if (!client || !client->connected || !ed) return;

    int rw = client->width;
    int rh = client->height;
    if (rw < 10) rw = 10;
    if (rh < 4) rh = 4;

    int content_width = tui_input_content_width(client);
    richtext_span_t rows[EDITOR_ROW_TABLE];
    size_t row_count = richtext_wrap(editor_text(ed), content_width, rows,
                                     EDITOR_ROW_TABLE);
    if (row_count == 0) {
        rows[0].offset = 0;
        rows[0].len = 0;
        row_count = 1;
    }

    int caret_row = editor_caret_row(ed, content_width);
    int visible = (int)row_count;
    if (visible > EDITOR_MAX_ROWS) visible = EDITOR_MAX_ROWS;

    /* Scroll the region so the caret stays inside it. */
    int first = caret_row - visible + 1;
    if (first < 0) first = 0;
    if (first > (int)row_count - visible) first = (int)row_count - visible;

    /* The gauge counts what the record layer will store, so a newline shows
     * as the two bytes it costs. */
    size_t input_bytes = message_log_encoded_length(editor_text(ed));
    int gauge_width = 0;
    char gauge[64] = "";
    if (input_bytes > (MAX_MESSAGE_LEN * 8) / 10) {  /* > 80 % */
        size_t remaining = (input_bytes < MAX_MESSAGE_LEN)
                           ? (MAX_MESSAGE_LEN - 1 - input_bytes) : 0;
        const char *color =
            (input_bytes > (MAX_MESSAGE_LEN * 95) / 100) ? "\033[1;33m"
                                                         : "\033[2;37m";
        char digits[12];

        snprintf(gauge, sizeof(gauge), "%s… %zu B\033[0m", color, remaining);
        snprintf(digits, sizeof(digits), "%zu", remaining);
        gauge_width = 4 + (int)strlen(digits) + 2;
    }

    char buffer[(size_t)EDITOR_MAX_ROWS * (MAX_MESSAGE_LEN + 64) + 512];
    size_t pos = 0;
    buffer[0] = '\0';

    for (int i = 0; i < visible; i++) {
        const richtext_span_t *span = &rows[first + i];
        int screen_row = rh - visible + 1 + i;
        char row_text[MAX_MESSAGE_LEN];

        snprintf(row_text, sizeof(row_text), "%.*s", (int)span->len,
                 editor_text(ed) + span->offset);

        /* The prompt marks the first row of the message; continuation rows
         * align under it so a wrapped line reads as one message. */
        const char *prompt = (first + i == 0) ? "\033[2;37m›\033[0m " : "  ";

        if (gauge_width > 0 && i == visible - 1) {
            int displayed_width = utf8_string_width(row_text);
            int padding = rw - 2 - displayed_width - gauge_width;
            if (padding < 1) padding = 1;
            buffer_appendf(buffer, sizeof(buffer), &pos,
                           "\033[%d;1H" ANSI_CLEAR_LINE "%s%s%*s%s",
                           screen_row, prompt, row_text, padding, "", gauge);
        } else {
            buffer_appendf(buffer, sizeof(buffer), &pos,
                           "\033[%d;1H" ANSI_CLEAR_LINE "%s%s",
                           screen_row, prompt, row_text);
        }
    }

    int caret_screen_row = rh - visible + 1 + (caret_row - first);
    int caret_col = 3 + editor_caret_column(ed, content_width);
    if (caret_col < 3) caret_col = 3;
    if (caret_col > rw) caret_col = rw;
    buffer_appendf(buffer, sizeof(buffer), &pos, "\033[%d;%dH",
                   caret_screen_row, caret_col);

    client_send(client, buffer, pos);
}
```

The horizontal caret-following scroll and `editor_cursor_column()` go away:
text now wraps instead of scrolling sideways, so there is nothing to scroll.
Add `#include "richtext.h"` and `#include "message_log.h"` to `src/tui.c` if
they are not already there.

- [ ] **Step 6: Update the height call sites**

Each call must pass the current input row count.

- `src/tui.c:299` — inside `tui_render_screen`, which has no editor. Give
  `client` a cached `int input_rows;` field in `include/ssh_server.h`, set by
  `tui_render_input` on every render and initialised to 1 at session start,
  and pass `client->input_rows` here. This is one `int`, not a buffer, so the
  per-connection memory guard is unaffected.
- `src/input.c:279`, `:286`, `:744`, `:1051` — these all have `ed` in scope;
  pass `tui_input_rows(client, ed)`.

- [ ] **Step 7: Build and run everything**

Run: `make && cd tests/unit && make run`
Expected: PASS, all suites.

- [ ] **Step 8: Commit**

```bash
git add include/history_view.h include/ssh_server.h include/tui.h src/history_view.c src/tui.c src/input.c tests/unit/test_history_view.c
git commit -m "feat: grow the input region to the message's height"
```

---

### Task 5: The newline keys and display-line movement

**Files:**
- Modify: `src/input.c` (`handle_key`, the Enter branch at `:890`, the arrow branches at `:714-780`, the CSI-tilde handler at `:585-610`)

**Interfaces:**
- Consumes: `editor_insert_newline()`, `editor_move_up()`,
  `editor_move_down()`, `editor_move_home(ed, width)`,
  `editor_move_end(ed, width)` from Task 1; `tui_input_rows()` from Task 4;
  `message_log_encoded_length()` from Task 3.
- Produces: no new symbols.

Key decisions this task encodes:

- `\r` sends. `\n` (Ctrl+J) inserts a newline. Today `src/input.c:890` treats
  both as Enter; terminals in raw mode send `\r` for Enter, so `\n` is free.
- `Alt+Enter` is `ESC` followed by `\r`, and is honoured **only in the default
  keymap**. In the vim keymap `ESC` means NORMAL and must keep meaning that.
- `Up` / `Down` recall sent history only when the editor is empty, exactly as
  today. When it is not empty they move the caret between display rows.
- The vim COMMAND-line Enter at `src/input.c:1197` is unchanged.

- [ ] **Step 1: Send on `\r`, insert on `\n`**

At `src/input.c:890`, split the branch:

```c
            } else if (key == '\n') {  /* Ctrl+J — newline, never send */
                if (editor_insert_newline(ed)) {
                    tui_render_screen(client);
                    tui_render_input(client, ed);
                } else {
                    client_send(client, "\a", 1);
                }
                return true;
            } else if (key == '\r') {  /* Enter — send */
```

The rest of the existing Enter body is unchanged.

`tui_render_screen` is called before `tui_render_input` because the input
region may have just grown and the history region must be redrawn shorter.
Apply the same pairing everywhere the row count can change: newline
insertion, backspace, paste, and send.

- [ ] **Step 2: Guard the encoded limit on send**

Immediately inside the `key == '\r'` branch, before the existing body:

```c
                if (message_log_encoded_length(editor_text(ed)) >=
                    MAX_MESSAGE_LEN) {
                    /* Escaping pushed the message past the field limit.  Keep
                     * the text and tell the user rather than dropping it. */
                    client_send(client, "\a", 1);
                    return true;
                }
```

- [ ] **Step 3: Alt+Enter in the default keymap**

In the ESC handling, where a following byte is read to distinguish a CSI
sequence, add a `\r` case before the plain-ESC fallthrough:

```c
                    } else if (seq[0] == '\r' &&
                               !tnt_keymap_uses_modes(client->keymap)) {
                        /* Alt+Enter.  Only in the default keymap: ESC is the
                         * way into NORMAL when the vim keymap is active. */
                        if (editor_insert_newline(ed)) {
                            tui_render_screen(client);
                            tui_render_input(client, ed);
                        } else {
                            client_send(client, "\a", 1);
                        }
                        return true;
```

- [ ] **Step 4: Up and Down move the caret when the editor is not empty**

At `src/input.c:753-757`, the Up branch currently returns early when the
editor is not empty. Replace that early return with movement:

```c
                        } else if (seq[1] == 'A') {  /* Up */
                            if (!tnt_keymap_uses_modes(client->keymap) &&
                                editor_len(ed) > 0) {
                                if (editor_move_up(ed, client->width - 3)) {
                                    tui_render_input(client, ed);
                                }
                                return true;
                            }
```

and give Down the mirror image, before its existing history-walk body:

```c
                        } else if (seq[1] == 'B') {  /* Down */
                            if (!tnt_keymap_uses_modes(client->keymap) &&
                                editor_len(ed) > 0) {
                                if (editor_move_down(ed, client->width - 3)) {
                                    tui_render_input(client, ed);
                                }
                                return true;
                            }
```

- [ ] **Step 5: Use one width everywhere**

Task 1 Step 6 put the literal `client->width - 3` at `src/input.c:597`,
`:604`, `:723`, and `:727`, and Step 4 above put it in the
`editor_move_up`/`editor_move_down` calls. Replace all six with
`tui_input_content_width(client)`, which Task 4 exported. The caret and the
renderer must lay the text out at the same width, or Home lands on the wrong
row.

Grep to confirm none is left:

```bash
grep -n 'client->width - 3' src/input.c
```

Expected: no output.

- [ ] **Step 6: Build and run everything**

Run: `make && cd tests/unit && make run && cd .. && PORT=12400 ./test_default_keymap.sh && PORT=12401 ./test_vim_keymap.sh && PORT=12402 ./test_cursor_editing.sh`
Expected: PASS. The vim suite passing unchanged is the evidence that the vim
key table was not disturbed.

- [ ] **Step 7: Commit**

```bash
git add include/tui.h src/input.c src/tui.c
git commit -m "feat: insert a newline with Ctrl+J and Alt+Enter"
```

---

### Task 6: End-to-end test and documentation

**Files:**
- Create: `tests/test_multiline_input.sh`
- Modify: `Makefile` (add the test to `integration-test`)
- Modify: `MAINTAINERS` (list the new test under CHAT INTERFACE)
- Modify: `tnt-chat.7` (the newline keys and the input region)
- Modify: `tnt-module-protocol.7` (a note that `plain_text` may contain newlines)

**Interfaces:**
- Consumes: everything above.
- Produces: nothing.

- [ ] **Step 1: Write the end-to-end test**

Create `tests/test_multiline_input.sh`, modelled on
`tests/test_cursor_editing.sh`. It asserts against `messages.log` rather than
the screen, because `stty` in an expect script does not reach the server's
idea of the terminal size.

```sh
#!/bin/sh
# End-to-end test: Ctrl+J composes a second line and Enter sends both.

PORT=${PORT:-12362}
PASS=0
FAIL=0
BIN="../tnt"
SERVER_PID=""
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-multiline-test.XXXXXX")

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$STATE_DIR"
}
trap cleanup EXIT

pass() { echo "✓ $1"; PASS=$((PASS + 1)); }
fail() { echo "✗ $1"; FAIL=$((FAIL + 1)); }

if ! command -v expect >/dev/null 2>&1; then
    echo "expect not installed; skipping multi-line input test"
    exit 0
fi
if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run make first."
    exit 1
fi

SSH_OPTS="-e none -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectionAttempts=3 -o ConnectTimeout=15 -p $PORT"

echo "=== TNT Multi-line Input Test ==="

TNT_LANG=en TNT_RATE_LIMIT=0 TNT_MAX_CONN_PER_IP=256 TNT_MAX_CONNECTIONS=256 \
    "$BIN" --bind 127.0.0.1 -p "$PORT" -d "$STATE_DIR" \
    >"$STATE_DIR/server.log" 2>&1 &
SERVER_PID=$!

SERVER_READY=0
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "✗ Server failed to start"
        sed -n '1,120p' "$STATE_DIR/server.log"
        exit 1
    fi
    if grep -q "TNT chat server listening" "$STATE_DIR/server.log"; then
        SERVER_READY=1
        break
    fi
    sleep 1
done
[ "$SERVER_READY" -eq 1 ] || { echo "✗ Server did not become ready"; exit 1; }
pass "server started"

SCRIPT="$STATE_DIR/compose.expect"
cat >"$SCRIPT" <<EOF
set timeout 15
spawn ssh $SSH_OPTS anonymous@127.0.0.1
expect "name"
send "liner\r"
expect "/help"
send "first"
send "\n"
send "second\r"
expect {
    timeout {}
}
send "/quit\r"
expect eof
EOF

expect -f "$SCRIPT" >"$STATE_DIR/session.log" 2>&1

if grep -qF '|liner|first\\nsecond' "$STATE_DIR/messages.log"; then
    pass "Ctrl+J composed a second line and Enter sent one record"
else
    fail "multi-line record not found"
    sed -n '1,20p' "$STATE_DIR/messages.log"
fi

if [ "$(grep -c '|liner|' "$STATE_DIR/messages.log")" = "1" ]; then
    pass "the two lines are one record, not two"
else
    fail "expected exactly one record from liner"
fi

if [ "$(head -n 1 "$STATE_DIR/messages.log")" = "#tnt-message-log v2" ]; then
    pass "a fresh log is v2"
else
    fail "header missing"
fi

echo ""
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All tests passed" || echo "Some tests failed"
exit "$FAIL"
```

Make it executable: `chmod +x tests/test_multiline_input.sh`

- [ ] **Step 2: Lock in that `exec post` stays single-line**

Task 3 claims `exec_command_post` already rejects a newline through
`utf8_contains_control`. Prove it rather than trusting it. Append to
`tests/test_exec_mode.sh`, beside the existing `post` cases:

```sh
MULTILINE_POST=$(ssh $SSH_OPTS localhost "post first
second" 2>&1)
MULTILINE_STATUS=$?
if [ "$MULTILINE_STATUS" -ne 0 ]; then
    echo "✓ exec post refuses a newline"
    PASS=$((PASS + 1))
else
    echo "✗ exec post accepted a newline"
    printf '%s\n' "$MULTILINE_POST"
    FAIL=$((FAIL + 1))
fi
```

Run: `make && cd tests && PORT=12403 ./test_exec_mode.sh`
Expected: PASS. If it fails, `exec post` does admit a newline and Task 3's
claim was wrong — add the explicit rejection to `exec_command_post` before
continuing.

- [ ] **Step 3: Register the test**

In `Makefile`, after the `test_log_migration.sh` line in `integration-test`:

```make
	@cd tests && PORT=$$(($${PORT:-2222} + 14)) ./test_multiline_input.sh
```

In `MAINTAINERS`, under the CHAT INTERFACE section that already lists
`tests/test_interactive_input.sh`:

```
F: tests/test_multiline_input.sh
```

- [ ] **Step 4: Run it**

Run: `make && cd tests && PORT=12362 ./test_multiline_input.sh`
Expected: PASS.

- [ ] **Step 5: Documentation**

In `tnt-chat.7`, in the key table for the default keymap, document that
`Enter` sends and `Ctrl+J` or `Alt+Enter` inserts a line break, that `Alt+Enter`
is default-keymap only because `ESC` enters NORMAL in vim mode, and that the
input region grows to at most six rows and then scrolls. In the vim section,
document `Ctrl+J` in INSERT. Bump the `.TH` date to the day of the change,
keeping the version field as it is. Manual page source lines must stay within
80 columns and must not reference `.md` files or `docs/`.

In `tnt-module-protocol.7`, add one sentence to the `plain_text` description:
a module's `plain_text` may contain newlines, which are stored escaped, and
every other control character is still rejected. Bump its `.TH` date too.

- [ ] **Step 6: Gates**

Run: `make test`
Run: `make release-check`
Expected: both green.

- [ ] **Step 7: Commit and open the pull request**

```bash
git add tests/test_multiline_input.sh Makefile MAINTAINERS tnt-chat.7 tnt-module-protocol.7
git commit -m "test: compose and send a multi-line message end to end"
```

Open the pull request against `main`, describing the three deviations from the
spec at the top.

---

## Sprint exit criteria

- `Ctrl+J` and `Alt+Enter` insert a line break; `Enter` still sends.
- A pasted block keeps its line breaks; `\r\n` becomes one break; a tab is
  still a space.
- The input region grows from 1 to 6 rows and the history region shrinks by
  the same amount; beyond 6 rows the region scrolls and the caret stays
  visible.
- A sent two-line message is one record in `messages.log`, stored escaped, and
  renders across two rows under one `[time] user:` prefix.
- The length gauge and the send guard both count encoded bytes.
- `exec post` still refuses a newline.
- The vim regression suite passes unchanged.
