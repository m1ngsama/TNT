#ifndef HISTORY_VIEW_H
#define HISTORY_VIEW_H

#include "message.h"

int history_view_height(int terminal_height);
int history_view_max_scroll(int message_count, int view_height);
void history_view_scroll_to_latest(int *scroll_pos, bool *follow_tail,
                                   int message_count, int view_height);
void history_view_scroll_to_oldest(int *scroll_pos, bool *follow_tail);
void history_view_scroll_by(int *scroll_pos, bool *follow_tail,
                            int message_count, int view_height, int delta);
int history_view_latest_start_for_height(const message_t *messages, int count,
                                         int height);

#endif /* HISTORY_VIEW_H */
