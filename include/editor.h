#ifndef EDITOR_H
#define EDITOR_H

#include "common.h"
#include "richtext.h"

#include <stdbool.h>
#include <stddef.h>

/* Display rows the input region may occupy before it scrolls internally. */
#define EDITOR_MAX_ROWS 6

/* Upper bound on one layout pass.  A message longer than this many display
 * rows is still edited correctly; only the rows past the table are invisible
 * to the geometry helpers, which never need more than the caret's row. */
#define EDITOR_ROW_TABLE 128

/* A message buffer with a cursor, which may contain newlines.
 *
 * Every mutation keeps three invariants: the buffer stays NUL-terminated,
 * `cursor` never exceeds `len`, and `cursor` always sits on a cluster
 * boundary, so an emoji or a flag is never split by editing. */
typedef struct editor {
    char buf[MAX_MESSAGE_LEN];
    size_t len;      /* bytes in use, excluding the terminator */
    size_t cursor;   /* byte offset of the caret */
} editor_t;

void editor_reset(editor_t *ed);

/* Replaces the whole buffer and puts the cursor at the end.  False when the
 * text does not fit, leaving the editor unchanged. */
bool editor_set_text(editor_t *ed, const char *text);

const char *editor_text(const editor_t *ed);
size_t editor_len(const editor_t *ed);
size_t editor_cursor(const editor_t *ed);

/* Display columns occupied by the text before the cursor, counted from the
 * start of the whole buffer.  Callers laying out a multi-row input region
 * want editor_caret_column() instead. */
int editor_cursor_column(const editor_t *ed);

/* Display-line geometry at the given render width, which is treated as 1 when
 * below it.  Rows are always at least 1, including for an empty buffer.  A
 * newline is a hard break and a full row is a soft one, so these count what
 * the user actually sees. */
int editor_display_rows(const editor_t *ed, int width);
int editor_caret_row(const editor_t *ed, int width);
int editor_caret_column(const editor_t *ed, int width);

/* Inserts at the cursor.  False on overflow, leaving the editor unchanged. */
bool editor_insert_bytes(editor_t *ed, const char *bytes, size_t n);
bool editor_insert_newline(editor_t *ed);

bool editor_delete_prev_cluster(editor_t *ed);
bool editor_delete_next_cluster(editor_t *ed);
bool editor_delete_prev_word(editor_t *ed);
void editor_clear(editor_t *ed);

bool editor_move_left(editor_t *ed);
bool editor_move_right(editor_t *ed);
bool editor_move_prev_word(editor_t *ed);
bool editor_move_next_word(editor_t *ed);

/* Vertical movement and Home/End are scoped to the caret's display row, which
 * is what the user sees; they need the render width to know where the rows
 * are.  Up and Down preserve the caret's column, clamping to a shorter row. */
bool editor_move_up(editor_t *ed, int width);
bool editor_move_down(editor_t *ed, int width);
void editor_move_home(editor_t *ed, int width);
void editor_move_end(editor_t *ed, int width);

#endif /* EDITOR_H */
