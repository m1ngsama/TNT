# Approachable UI — Stage 1: Text Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every message render at its true width — emoji, combining marks, and flags included — and wrap long lines instead of truncating them.

**Architecture:** Widen `utf8` from per-codepoint to per-cluster measurement using four bounded cluster rules, add a `richtext` module that owns width-aware wrapping, then make the history view count display lines rather than assuming one line per message. All three are pure functions with unit tests; only the last touches rendering.

**Tech Stack:** C11, no new dependencies. Unit tests use the repository's existing `TEST`/`RUN_TEST` macro style under `tests/unit/`.

**Spec:** `docs/superpowers/specs/2026-09-07-approachable-ui-design.md`

## Global Constraints

- C11, `-Wall -Wextra`, no new third-party dependencies. libssh stays the only external library.
- Per-connection resident cost must not exceed 1.3.1. The deployment runs under a 32 MiB cgroup baseline with `client_capacity` 50.
- No new control character is admitted into message content in this stage. `\n` arrives in Stage 3; every other C0/C1 byte and DEL stays rejected forever.
- Every new module ships the full set: `include/x.h`, `src/x.c`, `tests/unit/test_x.c`, a `TESTS` entry plus build rule plus `run` recipe line in `tests/unit/Makefile`, a `MAINTAINERS` entry, and a `.gitignore` line for the test binary. `scripts/check_maintainers.sh` and `tests/test_manpages.sh` both gate this.
- Manual pages are the only reference documentation. Portable man macros, no `tbl`, source lines within 80 columns.
- `make release-check` must pass before the final commit of the stage.
- Commit subjects are lowercase `type: subject`. Work lands through a pull request.

---

### Task 1: Cluster-aware width in `utf8`

The current `utf8_char_width()` returns 1 for every emoji, so each emoji in the
room shifts alignment by one column. This task adds emoji and zero-width
handling, then introduces cluster iteration so a ZWJ sequence or a flag counts
as one unit of width 2.

**Files:**
- Modify: `include/utf8.h`
- Modify: `src/utf8.c:53-83` (`utf8_char_width`), and `utf8_string_width`
- Test: `tests/unit/test_utf8.c`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `int utf8_char_width(uint32_t codepoint)` — unchanged signature, corrected ranges. Emoji return 2, combining marks and zero-width codepoints return 0.
  - `size_t utf8_cluster_length(const char *str)` — byte length of the cluster starting at `str`; 0 when `str` is NULL or empty.
  - `int utf8_cluster_width(const char *str)` — display columns of that cluster.
  - `int utf8_string_width(const char *str)` — unchanged signature, now the sum of cluster widths.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/test_utf8.c`, and add the `RUN_TEST` lines inside `main`:

```c
TEST(utf8_width_emoji_is_two_columns) {
    assert(utf8_string_width("😌") == 2);
    assert(utf8_string_width("🎲") == 2);
    assert(utf8_string_width("a😌b") == 4);
}

TEST(utf8_width_zero_width_codepoints) {
    /* e + U+0301 COMBINING ACUTE ACCENT renders in one column. */
    assert(utf8_string_width("e\xCC\x81") == 1);
    /* U+200B ZERO WIDTH SPACE */
    assert(utf8_string_width("\xE2\x80\x8B") == 0);
}

TEST(utf8_width_variation_selector_promotes_to_emoji) {
    /* U+2764 alone is a narrow symbol; with U+FE0F it is emoji-wide. */
    assert(utf8_string_width("\xE2\x9D\xA4") == 1);
    assert(utf8_string_width("\xE2\x9D\xA4\xEF\xB8\x8F") == 2);
}

TEST(utf8_cluster_zwj_sequence_is_one_unit) {
    const char *family = "👨\xE2\x80\x8D👩\xE2\x80\x8D👧";
    assert(utf8_cluster_length(family) == strlen(family));
    assert(utf8_cluster_width(family) == 2);
    assert(utf8_string_width(family) == 2);
}

TEST(utf8_cluster_regional_indicator_pair_is_one_flag) {
    const char *flag = "🇨🇳";
    assert(utf8_cluster_length(flag) == strlen(flag));
    assert(utf8_cluster_width(flag) == 2);
}

TEST(utf8_cluster_length_plain_characters) {
    assert(utf8_cluster_length("A") == 1);
    assert(utf8_cluster_length("中") == 3);
    assert(utf8_cluster_length("") == 0);
    assert(utf8_cluster_length(NULL) == 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tests/unit && make test_utf8 && ./test_utf8`
Expected: compile error, `utf8_cluster_length` is not declared.

- [ ] **Step 3: Declare the new interface**

Add to `include/utf8.h`, below `utf8_char_width`:

```c
/* Byte length of the grapheme-ish cluster starting at str.  Combining marks,
 * variation selectors, ZWJ sequences, and regional-indicator pairs join their
 * base character.  Returns 0 for NULL or an empty string. */
size_t utf8_cluster_length(const char *str);

/* Display columns occupied by the cluster starting at str. */
int utf8_cluster_width(const char *str);
```

- [ ] **Step 4: Correct the width table**

In `src/utf8.c`, inside `utf8_char_width`, insert before the final
`return 1;`:

```c
    /* Zero-width: combining marks, variation selectors, joiners, and the
     * zero-width space family.  These attach to a base character and add no
     * columns of their own. */
    if ((codepoint >= 0x0300 && codepoint <= 0x036F) ||   /* Combining Diacritical */
        (codepoint >= 0x1AB0 && codepoint <= 0x1AFF) ||   /* Combining Extended */
        (codepoint >= 0x1DC0 && codepoint <= 0x1DFF) ||   /* Combining Supplement */
        (codepoint >= 0x20D0 && codepoint <= 0x20FF) ||   /* Combining Symbols */
        (codepoint >= 0xFE00 && codepoint <= 0xFE0F) ||   /* Variation Selectors */
        (codepoint >= 0xFE20 && codepoint <= 0xFE2F) ||   /* Combining Half Marks */
        (codepoint >= 0x1160 && codepoint <= 0x11FF) ||   /* Hangul Jamo medial/final */
        codepoint == 0x200B || codepoint == 0x200C ||
        codepoint == 0x200D ||                            /* ZWSP, ZWNJ, ZWJ */
        (codepoint >= 0x2060 && codepoint <= 0x2064)) {
        return 0;
    }

    /* Emoji and other wide pictographs. */
    if ((codepoint >= 0x1F300 && codepoint <= 0x1F5FF) ||  /* Misc Pictographs */
        (codepoint >= 0x1F600 && codepoint <= 0x1F64F) ||  /* Emoticons */
        (codepoint >= 0x1F680 && codepoint <= 0x1F6FF) ||  /* Transport */
        (codepoint >= 0x1F900 && codepoint <= 0x1F9FF) ||  /* Supplemental */
        (codepoint >= 0x1FA70 && codepoint <= 0x1FAFF) ||  /* Extended-A */
        (codepoint >= 0x1F000 && codepoint <= 0x1F0FF) ||  /* Tiles and cards */
        (codepoint >= 0x1F1E6 && codepoint <= 0x1F1FF)) {  /* Regional indicators */
        return 2;
    }
```

- [ ] **Step 5: Implement cluster iteration**

Add to `src/utf8.c`, after `utf8_char_width`:

```c
static bool codepoint_is_regional_indicator(uint32_t cp) {
    return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

static bool codepoint_joins_previous(uint32_t cp) {
    /* Anything that renders as part of the preceding cluster. */
    return utf8_char_width(cp) == 0;
}

size_t utf8_cluster_length(const char *str) {
    int used = 0;
    uint32_t base;
    size_t len;

    if (!str || *str == '\0') {
        return 0;
    }

    base = utf8_decode(str, &used);
    if (used <= 0) {
        return 1;
    }
    len = (size_t)used;

    for (;;) {
        const char *next = str + len;
        int next_used = 0;
        uint32_t cp;

        if (*next == '\0') {
            break;
        }

        cp = utf8_decode(next, &next_used);
        if (next_used <= 0) {
            break;
        }

        if (cp == 0x200D) {
            /* A joiner always pulls in whatever follows it. */
            int joined_used = 0;
            const char *joined = next + next_used;

            if (*joined == '\0') {
                break;
            }
            utf8_decode(joined, &joined_used);
            if (joined_used <= 0) {
                break;
            }
            len += (size_t)next_used + (size_t)joined_used;
            continue;
        }

        if (codepoint_joins_previous(cp)) {
            len += (size_t)next_used;
            continue;
        }

        if (codepoint_is_regional_indicator(base) &&
            codepoint_is_regional_indicator(cp)) {
            /* A flag is exactly two indicators; a third starts a new flag. */
            len += (size_t)next_used;
        }
        break;
    }

    return len;
}

int utf8_cluster_width(const char *str) {
    size_t len = utf8_cluster_length(str);
    size_t offset = 0;
    int width = 0;
    bool emoji_presentation = false;

    if (len == 0) {
        return 0;
    }

    while (offset < len) {
        int used = 0;
        uint32_t cp = utf8_decode(str + offset, &used);

        if (used <= 0) {
            break;
        }
        if (cp == 0xFE0F) {
            emoji_presentation = true;
        }
        if (width == 0) {
            width = utf8_char_width(cp);
        }
        offset += (size_t)used;
    }

    if (emoji_presentation && width < 2) {
        width = 2;
    }
    return width == 0 ? 0 : width;
}
```

Then rewrite `utf8_string_width` to walk clusters:

```c
int utf8_string_width(const char *str) {
    int width = 0;

    if (!str) {
        return 0;
    }
    while (*str) {
        size_t len = utf8_cluster_length(str);

        if (len == 0) {
            break;
        }
        width += utf8_cluster_width(str);
        str += len;
    }
    return width;
}
```

- [ ] **Step 6: Run the tests**

Run: `cd tests/unit && make test_utf8 && ./test_utf8`
Expected: PASS, including the pre-existing CJK and ASCII cases.

- [ ] **Step 7: Commit**

```bash
git add include/utf8.h src/utf8.c tests/unit/test_utf8.c
git commit -m "fix: measure emoji and combining marks at true width"
```

---

### Task 2: Cluster-safe truncation

`utf8_truncate` currently cuts on codepoint boundaries, so it can split a flag
or a ZWJ family into a broken half-cluster. Rendering still truncates in
several places, so this must be correct before wrapping is introduced.

**Files:**
- Modify: `src/utf8.c` (`utf8_truncate`, `utf8_ansi_truncate`)
- Test: `tests/unit/test_utf8.c`

**Interfaces:**
- Consumes: `utf8_cluster_length`, `utf8_cluster_width` from Task 1.
- Produces: no signature change. `utf8_truncate(char *str, int max_width)` now never leaves a partial cluster.

- [ ] **Step 1: Write the failing test**

```c
TEST(utf8_truncate_never_splits_a_cluster) {
    char buf[64];

    /* Two flags, each two columns.  A three-column budget must keep one. */
    snprintf(buf, sizeof(buf), "🇨🇳🇯🇵");
    utf8_truncate(buf, 3);
    assert(strcmp(buf, "🇨🇳") == 0);

    /* A ZWJ family is indivisible: a one-column budget keeps nothing. */
    snprintf(buf, sizeof(buf), "👨\xE2\x80\x8D👩");
    utf8_truncate(buf, 1);
    assert(buf[0] == '\0');
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tests/unit && make test_utf8 && ./test_utf8`
Expected: FAIL — the truncated buffer holds a partial cluster.

- [ ] **Step 3: Implement cluster-safe truncation**

Replace the body of `utf8_truncate` in `src/utf8.c`:

```c
void utf8_truncate(char *str, int max_width) {
    size_t offset = 0;
    int width = 0;

    if (!str || max_width < 0) {
        return;
    }

    while (str[offset]) {
        size_t len = utf8_cluster_length(str + offset);
        int cluster_width = utf8_cluster_width(str + offset);

        if (len == 0 || width + cluster_width > max_width) {
            break;
        }
        width += cluster_width;
        offset += len;
    }
    str[offset] = '\0';
}
```

- [ ] **Step 4: Run the tests**

Run: `cd tests/unit && make test_utf8 && ./test_utf8`
Expected: PASS.

- [ ] **Step 5: Apply the same stepping to the ANSI variant**

`utf8_ansi_truncate` keeps a second, independent codepoint loop at
`src/utf8.c:218-234`. Message content never carries ANSI — only
server-generated prefixes do — so stepping by cluster between escape sequences
is safe. Replace that loop body:

```c
        size_t cluster_len = utf8_cluster_length(p);
        int cluster_width = utf8_cluster_width(p);

        if (cluster_len == 0) {
            break;
        }
        if (width + cluster_width > max_width) {
            truncated = true;
            break;
        }
        if (pos + cluster_len >= dst_size) {
            truncated = true;
            break;
        }

        memcpy(dst + pos, p, cluster_len);
        pos += cluster_len;
        width += cluster_width;
        p += cluster_len;
```

The surrounding escape-sequence handling, the `truncated` flag, and the
trailing `ANSI_RESET` repair are unchanged. `bytes_read` becomes unused in this
function — delete its declaration so `-Wall -Wextra` stays clean.

Run: `cd tests/unit && make test_utf8 && ./test_utf8`
Expected: PASS, including the pre-existing `utf8_ansi_truncate` cases.

- [ ] **Step 6: Commit**

```bash
git add src/utf8.c tests/unit/test_utf8.c
git commit -m "fix: keep truncation on cluster boundaries"
```

---

### Task 3: `richtext` module with width-aware wrapping

Wrapping is the half of "long messages are unreadable" that truncation cannot
fix. The module is created here with wrapping only; Stage 4 adds markdown
parsing to the same module.

**Files:**
- Create: `include/richtext.h`
- Create: `src/richtext.c`
- Create: `tests/unit/test_richtext.c`
- Modify: `tests/unit/Makefile` (`TESTS` list, build rule, `run` recipe)
- Modify: `MAINTAINERS`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: `utf8_cluster_length`, `utf8_cluster_width` from Task 1.
- Produces:
  - `typedef struct { size_t offset; size_t len; } richtext_span_t;`
  - `size_t richtext_wrap(const char *text, int width, richtext_span_t *out, size_t max_out)` — fills `out` with one span per display line and returns the number of lines written. A `\n` in `text` is a hard break. Returns 0 when `text` is NULL, `width < 1`, or `max_out` is 0.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/test_richtext.c`:

```c
/* Unit tests for width-aware wrapping */
#include "../../include/richtext.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define TEST(name) static void test_##name()
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    test_##name(); \
    printf("✓\n"); \
    tests_passed++; \
} while(0)

static int tests_passed = 0;

static int span_equals(const char *text, richtext_span_t span,
                       const char *expected) {
    size_t len = strlen(expected);
    return span.len == len && memcmp(text + span.offset, expected, len) == 0;
}

TEST(wrap_short_text_is_one_line) {
    richtext_span_t spans[4];
    const char *text = "hello";

    assert(richtext_wrap(text, 20, spans, 4) == 1);
    assert(span_equals(text, spans[0], "hello"));
}

TEST(wrap_breaks_at_a_space) {
    richtext_span_t spans[4];
    const char *text = "hello world";

    assert(richtext_wrap(text, 7, spans, 4) == 2);
    assert(span_equals(text, spans[0], "hello"));
    assert(span_equals(text, spans[1], "world"));
}

TEST(wrap_hard_breaks_when_no_space_fits) {
    richtext_span_t spans[4];
    const char *text = "abcdefgh";

    assert(richtext_wrap(text, 3, spans, 4) == 3);
    assert(span_equals(text, spans[0], "abc"));
    assert(span_equals(text, spans[1], "def"));
    assert(span_equals(text, spans[2], "gh"));
}

TEST(wrap_never_splits_a_wide_character) {
    richtext_span_t spans[4];
    const char *text = "中文中文";

    /* Three columns hold one double-width character, not one and a half. */
    assert(richtext_wrap(text, 3, spans, 4) == 4);
    assert(span_equals(text, spans[0], "中"));
}

TEST(wrap_never_splits_an_emoji_cluster) {
    richtext_span_t spans[4];
    const char *text = "🇨🇳🇯🇵";

    assert(richtext_wrap(text, 3, spans, 4) == 2);
    assert(span_equals(text, spans[0], "🇨🇳"));
}

TEST(wrap_treats_newline_as_a_hard_break) {
    richtext_span_t spans[4];
    const char *text = "ab\ncd";

    assert(richtext_wrap(text, 20, spans, 4) == 2);
    assert(span_equals(text, spans[0], "ab"));
    assert(span_equals(text, spans[1], "cd"));
}

TEST(wrap_rejects_bad_arguments) {
    richtext_span_t spans[4];

    assert(richtext_wrap(NULL, 10, spans, 4) == 0);
    assert(richtext_wrap("x", 0, spans, 4) == 0);
    assert(richtext_wrap("x", 10, spans, 0) == 0);
}

int main(void) {
    printf("Running richtext unit tests...\n\n");
    RUN_TEST(wrap_short_text_is_one_line);
    RUN_TEST(wrap_breaks_at_a_space);
    RUN_TEST(wrap_hard_breaks_when_no_space_fits);
    RUN_TEST(wrap_never_splits_a_wide_character);
    RUN_TEST(wrap_never_splits_an_emoji_cluster);
    RUN_TEST(wrap_treats_newline_as_a_hard_break);
    RUN_TEST(wrap_rejects_bad_arguments);
    printf("\nAll %d richtext tests passed!\n", tests_passed);
    return 0;
}
```

- [ ] **Step 2: Wire the test into the build**

In `tests/unit/Makefile`: add `RICHTEXT_SRC = ../../src/richtext.c` beside the
other `_SRC` variables, append `test_richtext` to `TESTS`, add the rule

```make
test_richtext: test_richtext.c $(RICHTEXT_SRC) $(UTF8_SRC)
	$(CC) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(PROJECT_LDLIBS) $(LDLIBS)
```

and add to the `run` recipe:

```make
	@echo "=== Running Richtext Tests ==="
	./test_richtext
	@echo ""
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cd tests/unit && make test_richtext`
Expected: FAIL — `include/richtext.h` does not exist.

- [ ] **Step 4: Write the header**

Create `include/richtext.h`:

```c
#ifndef RICHTEXT_H
#define RICHTEXT_H

#include <stddef.h>

/* One display line, expressed as a byte range into the source text. */
typedef struct {
    size_t offset;
    size_t len;
} richtext_span_t;

/* Split text into display lines of at most `width` columns.  A newline is a
 * hard break.  Clusters are never split, so a line may be narrower than
 * `width` when the next cluster does not fit.  Returns the number of spans
 * written, which is 0 for NULL text, width < 1, or max_out == 0. */
size_t richtext_wrap(const char *text, int width, richtext_span_t *out,
                     size_t max_out);

#endif /* RICHTEXT_H */
```

- [ ] **Step 5: Implement wrapping**

Create `src/richtext.c`:

```c
#include "richtext.h"

#include "utf8.h"

#include <string.h>

size_t richtext_wrap(const char *text, int width, richtext_span_t *out,
                     size_t max_out) {
    size_t line_start = 0;
    size_t offset = 0;
    size_t count = 0;
    size_t last_break = 0;      /* byte offset just past the last space */
    int line_width = 0;

    if (!text || !out || width < 1 || max_out == 0) {
        return 0;
    }

    while (text[offset] != '\0' && count < max_out) {
        size_t len;
        int cluster_width;

        if (text[offset] == '\n') {
            out[count].offset = line_start;
            out[count].len = offset - line_start;
            count++;
            offset++;
            line_start = offset;
            last_break = 0;
            line_width = 0;
            continue;
        }

        len = utf8_cluster_length(text + offset);
        if (len == 0) {
            break;
        }
        cluster_width = utf8_cluster_width(text + offset);

        if (line_width + cluster_width > width && offset > line_start) {
            size_t line_end = (last_break > line_start) ? last_break : offset;

            out[count].offset = line_start;
            out[count].len = line_end - line_start;
            /* Drop the space that caused the break. */
            while (out[count].len > 0 &&
                   text[out[count].offset + out[count].len - 1] == ' ') {
                out[count].len--;
            }
            count++;

            line_start = line_end;
            while (text[line_start] == ' ') {
                line_start++;
            }
            offset = line_start;
            last_break = 0;
            line_width = 0;
            continue;
        }

        offset += len;
        line_width += cluster_width;
        if (text[offset - len] == ' ') {
            last_break = offset;
        }
    }

    if (count < max_out && offset > line_start) {
        out[count].offset = line_start;
        out[count].len = offset - line_start;
        count++;
    } else if (count == 0 && count < max_out) {
        out[count].offset = 0;
        out[count].len = 0;
        count++;
    }

    return count;
}
```

- [ ] **Step 6: Run the tests**

Run: `cd tests/unit && make test_richtext && ./test_richtext`
Expected: PASS, all seven cases.

- [ ] **Step 7: Register the module with the repository gates**

In `MAINTAINERS`, add to the `INTERACTIVE TUI` section:

```
F: include/richtext.h
F: src/richtext.c
F: tests/unit/test_richtext.c
```

In `.gitignore`, beside the other unit test binaries:

```
tests/unit/test_richtext
```

Run: `scripts/check_maintainers.sh`
Expected: `check-maintainers: ok`

- [ ] **Step 8: Commit**

```bash
git add include/richtext.h src/richtext.c tests/unit/test_richtext.c \
        tests/unit/Makefile MAINTAINERS .gitignore
git commit -m "feat: add width-aware text wrapping"
```

---

### Task 4: Wrap messages in the history view

`history_view_latest_start_for_height()` already counts rows — one per message,
plus one for a date divider. Wrapping means a message can cost more than one
row, so the row accounting and the scroll bound both need the render width.

**Files:**
- Modify: `include/history_view.h`
- Modify: `src/history_view.c:18-21`, `:54-83`
- Modify: `src/tui.c:313-400` (call sites), `src/tui.c:110-155` (message line rendering)
- Test: `tests/unit/test_history_view.c`
- Modify: `tests/unit/Makefile` (link `RICHTEXT_SRC` and `UTF8_SRC` into `test_history_view`)

**Interfaces:**
- Consumes: `richtext_wrap` and `richtext_span_t` from Task 3.
- Produces:
  - `int history_view_message_lines(const message_t *msg, int width)` — display rows one message occupies, minimum 1.
  - `int history_view_max_scroll(const message_t *messages, int message_count, int view_height, int width)` — signature gains `messages` and `width`.
  - `history_view_scroll_to_latest`, `history_view_scroll_by`, and `history_view_latest_start_for_height` gain a trailing `int width` parameter.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/test_history_view.c`:

```c
TEST(history_view_counts_wrapped_message_rows) {
    message_t msg = {0};

    snprintf(msg.username, sizeof(msg.username), "u");
    snprintf(msg.content, sizeof(msg.content),
             "aaaaaaaaaa bbbbbbbbbb cccccccccc");

    /* Wide enough for one row. */
    assert(history_view_message_lines(&msg, 200) == 1);
    /* Narrow enough that the content needs more than one row. */
    assert(history_view_message_lines(&msg, 20) > 1);
}

TEST(history_view_message_lines_minimum_is_one) {
    message_t msg = {0};

    snprintf(msg.username, sizeof(msg.username), "u");
    snprintf(msg.content, sizeof(msg.content), "x");
    assert(history_view_message_lines(&msg, 1) == 1);
    assert(history_view_message_lines(&msg, 0) == 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tests/unit && make test_history_view && ./test_history_view`
Expected: compile error, `history_view_message_lines` is not declared.

- [ ] **Step 3: Declare and implement row counting**

Add to `include/history_view.h` — the cap lives in the header because
`src/tui.c` sizes its own span array with it in Task 4 Step 8:

```c
/* Upper bound on display rows one message may occupy. */
#define HISTORY_VIEW_MAX_WRAPPED_ROWS 64

/* Display rows one message occupies at the given render width.  Always at
 * least 1, including for width <= 0. */
int history_view_message_lines(const message_t *msg, int width);
```

Add to `src/history_view.c`:

```c
#include "richtext.h"

int history_view_message_lines(const message_t *msg, int width) {
    richtext_span_t spans[HISTORY_VIEW_MAX_WRAPPED_ROWS];
    size_t rows;

    if (!msg || width < 1) {
        return 1;
    }

    rows = richtext_wrap(msg->content, width, spans,
                         HISTORY_VIEW_MAX_WRAPPED_ROWS);
    return rows < 1 ? 1 : (int)rows;
}
```

- [ ] **Step 4: Run the tests**

Run: `cd tests/unit && make test_history_view && ./test_history_view`
Expected: PASS.

- [ ] **Step 5: Commit the pure part**

```bash
git add include/history_view.h src/history_view.c tests/unit/test_history_view.c \
        tests/unit/Makefile
git commit -m "feat: count wrapped rows per message"
```

- [ ] **Step 6: Thread width through the scroll math**

Every one of these functions needs the message array as well as the width,
because a scroll bound can no longer be computed from a count alone. The new
header declarations, replacing lines 18-28 and 36-37 of
`include/history_view.h`:

```c
int history_view_max_scroll(const message_t *messages, int message_count,
                            int view_height, int width);
void history_view_scroll_to_latest(int *scroll_pos, bool *follow_tail,
                                   const message_t *messages,
                                   int message_count, int view_height,
                                   int width);
void history_view_scroll_by(int *scroll_pos, bool *follow_tail,
                            const message_t *messages, int message_count,
                            int view_height, int width, int delta);
int history_view_latest_start_for_height(const message_t *messages, int count,
                                         int height, int width);
```

In `src/history_view.c`, `history_view_max_scroll` becomes the index of the
oldest message in the newest screenful, which is exactly what the existing row
walk already computes:

```c
int history_view_max_scroll(const message_t *messages, int message_count,
                            int view_height, int width) {
    if (!messages || message_count <= 0) {
        return 0;
    }
    return history_view_latest_start_for_height(messages, message_count,
                                                view_height, width);
}
```

and inside `history_view_latest_start_for_height`, the per-message row cost
replaces `rows++`:

```c
        rows += history_view_message_lines(&messages[candidate], width);
```

`history_view_scroll_to_latest` and `history_view_scroll_by` keep their bodies
and forward the new arguments to `history_view_max_scroll`.

- [ ] **Step 7: Update every call site**

Run: `grep -rn "history_view_" src/ | grep -v "^src/history_view.c"`
Update each call in `src/tui.c` and `src/input.c` to pass the render width
already available there (`client->width` clamped exactly as the surrounding
code clamps it).

- [ ] **Step 8: Render wrapped lines**

`tui_format_message_line` currently produces one string per message and
truncates at `src/tui.c:148`. Give it an explicit row index so the caller can
ask for each display row in turn, and have it wrap instead of truncate:

```c
/* Renders display row `row` of msg into buffer.  Returns false when the
 * message has no such row, which is how the caller knows it is done. */
static bool tui_format_message_row(const message_t *msg, int row, int width,
                                   char *buffer, size_t buf_size) {
    richtext_span_t spans[HISTORY_VIEW_MAX_WRAPPED_ROWS];
    char prefix_plain[256];
    char content_row[MAX_MESSAGE_LEN];
    int prefix_width;
    int content_width;
    size_t rows;

    build_message_prefix(msg, prefix_plain, sizeof(prefix_plain));
    prefix_width = utf8_string_width(prefix_plain);
    content_width = width - prefix_width;
    if (content_width < 4) {
        content_width = 4;
    }

    rows = richtext_wrap(msg->content, content_width, spans,
                         HISTORY_VIEW_MAX_WRAPPED_ROWS);
    if (row < 0 || (size_t)row >= rows) {
        return false;
    }

    snprintf(content_row, sizeof(content_row), "%.*s",
             (int)spans[row].len, msg->content + spans[row].offset);

    if (row == 0) {
        render_message_with_prefix(msg, content_row, buffer, buf_size);
    } else {
        /* Continuation rows align under the first row's text. */
        snprintf(buffer, buf_size, "%*s%s", prefix_width, "", content_row);
    }
    return true;
}
```

`build_message_prefix` and `render_message_with_prefix` are the two halves of
the existing `src/tui.c:110-155` block, extracted verbatim so the system,
`*`-sender, and normal-sender variants keep their current formatting and
colours. The render loop at `src/tui.c:847` then iterates rows per message
instead of once per message, stopping when the view height is full.

- [ ] **Step 8b: Confirm nothing overflows the render buffer**

Wrapping multiplies the bytes a single message can contribute to one screen.
Run: `make asan && make integration-test PORT=13745`
Expected: no AddressSanitizer report; the render buffer grows through the
existing `client->render_buffer` reallocation path rather than a fixed array.

- [ ] **Step 9: Verify the build and the whole suite**

Run: `make && make unit-test && make integration-test PORT=13740`
Expected: build clean under `-Wall -Wextra`, all suites pass.

- [ ] **Step 10: Commit**

```bash
git add include/history_view.h src/history_view.c src/tui.c src/input.c
git commit -m "feat: wrap long messages instead of truncating them"
```

---

### Task 5: Integration test, documentation, and stage close

**Files:**
- Create: `tests/test_wide_text_view.sh`
- Modify: `Makefile` (`script-test` or `integration-test` recipe, matching how neighbouring tests are listed)
- Modify: `tnt-chat.7`

**Interfaces:**
- Consumes: everything above.
- Produces: no code interface.

- [ ] **Step 1: Write the integration test**

Create `tests/test_wide_text_view.sh`, modelled on `tests/test_empty_view.sh`.
It must launch the server with `TNT_MAX_CONN_PER_IP=256 TNT_MAX_CONNECTIONS=256`
and use a 15-iteration readiness poll, per the two established patterns in this
repository. Seed `messages.log` before starting the server with:

```sh
printf '%s|fixture|%s\n' "$seed_ts" \
    "😌 aaaaaaaaaa bbbbbbbbbb cccccccccc dddddddddd eeeeeeeeee ffffffffff" \
    >>"$STATE_DIR/messages.log"
```

Then connect with `expect` at `stty rows 12 columns 40` and assert that the
full text is present across the rendered rows — no ellipsis, no lost tail.

- [ ] **Step 2: Run it and watch it pass**

Run: `cd tests && PORT=13760 sh test_wide_text_view.sh`
Expected: all assertions pass.

- [ ] **Step 3: Register it**

Add the script to the same `Makefile` recipe that runs `test_empty_view.sh`.
`MAINTAINERS` needs no edit: the `TESTING` section already claims `tests/**`.

Run: `scripts/check_maintainers.sh`
Expected: `check-maintainers: ok`

- [ ] **Step 4: Update the manual page**

In `tnt-chat.7`, replace the sentence stating that long content is truncated
with one stating that long messages wrap and continuation rows align under the
first row. Keep source lines within 80 columns and use portable macros.

Run: `sh tests/test_manpages.sh`
Expected: 14 passed, 0 failed.

- [ ] **Step 5: Full gate**

Run: `make release-check`
Expected: `release preflight passed`.

- [ ] **Step 6: Commit and open the pull request**

```bash
git add tests/test_wide_text_view.sh Makefile MAINTAINERS tnt-chat.7
git commit -m "test: cover wide text rendering end to end"
git push -u origin feat/approachable-ui
gh pr create --base main --title "Render text at its true width" \
  --body "Stage 1 of docs/superpowers/specs/2026-09-07-approachable-ui-design.md"
```

---

## Stage exit criteria

- `😌` and other emoji occupy two columns everywhere: borders, status chips, truncation, and wrapping all agree.
- A message longer than the terminal width is fully readable across wrapped rows rather than cut off.
- No cluster is ever split, in truncation or in wrapping.
- `make release-check` passes; per-connection memory is unchanged from 1.3.1.
- Visual confirmation on the deployment host's live instance at port 2222.
