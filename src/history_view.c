#include "history_view.h"

static void message_date_key(const message_t *msg, char out[11]) {
    struct tm tmi;
    localtime_r(&msg->timestamp, &tmi);
    strftime(out, 11, "%Y-%m-%d", &tmi);
}

static int rendered_rows_for_slice(const message_t *messages, int start,
                                   int end) {
    int rows = 0;
    char last_date[11] = "";

    for (int i = start; i < end; i++) {
        char this_date[11];
        message_date_key(&messages[i], this_date);
        if (strcmp(this_date, last_date) != 0) {
            rows++;
            memcpy(last_date, this_date, sizeof(last_date));
        }
        rows++;
    }

    return rows;
}

int history_view_height(int terminal_height) {
    int height = terminal_height - 3;
    return height < 1 ? 1 : height;
}

int history_view_max_scroll(int message_count, int view_height) {
    int max_scroll = message_count - view_height;
    return max_scroll < 0 ? 0 : max_scroll;
}

void history_view_scroll_to_latest(int *scroll_pos, bool *follow_tail,
                                   int message_count, int view_height) {
    if (!scroll_pos || !follow_tail) return;
    *scroll_pos = history_view_max_scroll(message_count, view_height);
    *follow_tail = true;
}

void history_view_scroll_to_oldest(int *scroll_pos, bool *follow_tail) {
    if (!scroll_pos || !follow_tail) return;
    *scroll_pos = 0;
    *follow_tail = false;
}

void history_view_scroll_by(int *scroll_pos, bool *follow_tail,
                            int message_count, int view_height, int delta) {
    if (!scroll_pos || !follow_tail) return;

    int max_scroll = history_view_max_scroll(message_count, view_height);
    if (*follow_tail && delta < 0) {
        *scroll_pos = max_scroll;
    }

    *scroll_pos += delta;
    if (*scroll_pos < 0) {
        *scroll_pos = 0;
    } else if (*scroll_pos > max_scroll) {
        *scroll_pos = max_scroll;
    }
    *follow_tail = *scroll_pos >= max_scroll;
}

int history_view_latest_start_for_height(const message_t *messages, int count,
                                         int height) {
    int start = count;

    for (int candidate = count - 1; candidate >= 0; candidate--) {
        int rows = rendered_rows_for_slice(messages, candidate, count);
        if (rows > height) {
            break;
        }
        start = candidate;
    }

    if (start == count && count > 0) {
        start = count - 1;
    }
    return start;
}
