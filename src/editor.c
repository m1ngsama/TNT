#include "editor.h"

#include "utf8.h"

#include <string.h>

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

static size_t word_start_before(const editor_t *ed, size_t pos) {
    size_t at = pos;

    while (at > 0 && ed->buf[at - 1] == ' ') {
        at--;
    }
    while (at > 0 && ed->buf[at - 1] != ' ') {
        at--;
    }
    return at;
}

void editor_reset(editor_t *ed) {
    if (!ed) {
        return;
    }
    ed->buf[0] = '\0';
    ed->len = 0;
    ed->cursor = 0;
}

bool editor_set_text(editor_t *ed, const char *text) {
    size_t n;

    if (!ed || !text) {
        return false;
    }
    n = strlen(text);
    if (n >= sizeof(ed->buf)) {
        return false;
    }
    memcpy(ed->buf, text, n);
    ed->buf[n] = '\0';
    ed->len = n;
    ed->cursor = n;
    return true;
}

const char *editor_text(const editor_t *ed) {
    return ed ? ed->buf : "";
}

size_t editor_len(const editor_t *ed) {
    return ed ? ed->len : 0;
}

size_t editor_cursor(const editor_t *ed) {
    return ed ? ed->cursor : 0;
}

int editor_cursor_column(const editor_t *ed) {
    size_t offset = 0;
    int width = 0;

    if (!ed) {
        return 0;
    }
    while (offset < ed->cursor) {
        size_t len = utf8_cluster_length(ed->buf + offset);

        if (len == 0) {
            break;
        }
        width += utf8_cluster_width(ed->buf + offset);
        offset += len;
    }
    return width;
}

bool editor_insert_bytes(editor_t *ed, const char *bytes, size_t n) {
    if (!ed || !bytes || n == 0) {
        return false;
    }
    if (n > sizeof(ed->buf) - 1 - ed->len) {
        return false;
    }

    memmove(ed->buf + ed->cursor + n, ed->buf + ed->cursor,
            ed->len - ed->cursor);
    memcpy(ed->buf + ed->cursor, bytes, n);
    ed->len += n;
    ed->cursor += n;
    ed->buf[ed->len] = '\0';
    return true;
}

static void erase_range(editor_t *ed, size_t from, size_t to) {
    memmove(ed->buf + from, ed->buf + to, ed->len - to);
    ed->len -= to - from;
    ed->cursor = from;
    ed->buf[ed->len] = '\0';
}

bool editor_delete_prev_cluster(editor_t *ed) {
    size_t start;

    if (!ed || ed->cursor == 0) {
        return false;
    }
    start = cluster_start_before(ed, ed->cursor);
    erase_range(ed, start, ed->cursor);
    return true;
}

bool editor_delete_next_cluster(editor_t *ed) {
    size_t len;

    if (!ed || ed->cursor >= ed->len) {
        return false;
    }
    len = utf8_cluster_length(ed->buf + ed->cursor);
    if (len == 0) {
        return false;
    }
    erase_range(ed, ed->cursor, ed->cursor + len);
    return true;
}

bool editor_delete_prev_word(editor_t *ed) {
    size_t start;

    if (!ed || ed->cursor == 0) {
        return false;
    }
    start = word_start_before(ed, ed->cursor);
    erase_range(ed, start, ed->cursor);
    return true;
}

void editor_clear(editor_t *ed) {
    editor_reset(ed);
}

bool editor_move_left(editor_t *ed) {
    if (!ed || ed->cursor == 0) {
        return false;
    }
    ed->cursor = cluster_start_before(ed, ed->cursor);
    return true;
}

bool editor_move_right(editor_t *ed) {
    size_t len;

    if (!ed || ed->cursor >= ed->len) {
        return false;
    }
    len = utf8_cluster_length(ed->buf + ed->cursor);
    if (len == 0) {
        return false;
    }
    ed->cursor += len;
    if (ed->cursor > ed->len) {
        ed->cursor = ed->len;
    }
    return true;
}

bool editor_move_prev_word(editor_t *ed) {
    if (!ed || ed->cursor == 0) {
        return false;
    }
    ed->cursor = word_start_before(ed, ed->cursor);
    return true;
}

bool editor_move_next_word(editor_t *ed) {
    size_t at;

    if (!ed || ed->cursor >= ed->len) {
        return false;
    }
    at = ed->cursor;
    while (at < ed->len && ed->buf[at] != ' ') {
        at++;
    }
    while (at < ed->len && ed->buf[at] == ' ') {
        at++;
    }
    ed->cursor = at;
    return true;
}

void editor_move_home(editor_t *ed) {
    if (ed) {
        ed->cursor = 0;
    }
}

void editor_move_end(editor_t *ed) {
    if (ed) {
        ed->cursor = ed->len;
    }
}
