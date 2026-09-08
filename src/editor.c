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

/* A caret on a row boundary belongs to the row starting there, which is what
 * puts it on the new line after Ctrl+J. */
static size_t caret_row_index(const editor_t *ed, const richtext_span_t *rows,
                              size_t row_count) {
    size_t i;

    for (i = 0; i < row_count; i++) {
        if (ed->cursor < rows[i].offset + rows[i].len) {
            return i;
        }
        if (ed->cursor == rows[i].offset + rows[i].len &&
            (i + 1 == row_count || ed->cursor < rows[i + 1].offset)) {
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

/* Offset in `row` at or before `column`, always on a cluster boundary. */
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

bool editor_insert_newline(editor_t *ed) {
    return editor_insert_bytes(ed, "\n", 1);
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
