#ifndef HISTORY_VIEW_H
#define HISTORY_VIEW_H

#include "message.h"

/* Upper bound on display rows one message may occupy. */
#define HISTORY_VIEW_MAX_WRAPPED_ROWS 64

int history_view_height(int terminal_height);

/* Display rows one message occupies at the given render width.  Always at
 * least 1, including for width <= 0. */
int history_view_message_lines(const message_t *msg, int width);
int history_view_max_scroll(int message_count, int view_height);
void history_view_scroll_to_latest(int *scroll_pos, bool *follow_tail,
                                   int message_count, int view_height);
void history_view_scroll_to_oldest(int *scroll_pos, bool *follow_tail);
void history_view_scroll_by(int *scroll_pos, bool *follow_tail,
                            int message_count, int view_height, int delta);
int history_view_latest_start_for_height(const message_t *messages, int count,
                                         int height);

#endif /* HISTORY_VIEW_H */
