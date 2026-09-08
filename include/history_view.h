#ifndef HISTORY_VIEW_H
#define HISTORY_VIEW_H

#include "message.h"

/* Upper bound on display rows one message may occupy. */
#define HISTORY_VIEW_MAX_WRAPPED_ROWS 64

/* Rows left for history once the input region takes input_rows.  At least 1. */
int history_view_height(int terminal_height, int input_rows);

/* The renderer prints " HH:MM name: " ahead of the first row and indents
 * continuation rows to match, so content wraps at width minus that prefix.
 * Row counting and rendering must agree on it or the newest messages fall off
 * the bottom of a full screen. */
void history_view_message_time(const message_t *msg, char out[16]);
int history_view_message_prefix_width(const message_t *msg);
int history_view_message_content_width(const message_t *msg, int width);

/* Display rows one message occupies at the given render width.  Always at
 * least 1, including for width <= 0. */
int history_view_message_lines(const message_t *msg, int width);
int history_view_max_scroll(int message_count, int view_height);
void history_view_scroll_to_latest(int *scroll_pos, bool *follow_tail,
                                   int message_count, int view_height);
void history_view_scroll_to_oldest(int *scroll_pos, bool *follow_tail);
void history_view_scroll_by(int *scroll_pos, bool *follow_tail,
                            int message_count, int view_height, int delta);
/* Oldest message index that still fits in the newest screenful, accounting
 * for date dividers and for messages that wrap across several rows at the
 * given render width.  Callers that hold a message snapshot use this; the
 * cheap count-based bound above stays for the scroll-position clamp. */
int history_view_latest_start_for_height(const message_t *messages, int count,
                                         int height, int width);

#endif /* HISTORY_VIEW_H */
