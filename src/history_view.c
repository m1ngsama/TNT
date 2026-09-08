#include "history_view.h"

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

static void message_date_key(const message_t *msg, char out[11]) {
    if (msg->display_date[0] != '\0') {
        memcpy(out, msg->display_date, 11);
        return;
    }
    struct tm tmi;
    localtime_r(&msg->timestamp, &tmi);
    strftime(out, 11, "%Y-%m-%d", &tmi);
}

int history_view_height(int terminal_height, int input_rows) {
    int height;

    if (input_rows < 1) {
        input_rows = 1;
    }
    height = terminal_height - 3 - (input_rows - 1);
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
                                         int height, int width) {
    int start = count;
    int rows = 0;
    char next_date[11] = "";

    for (int candidate = count - 1; candidate >= 0; candidate--) {
        char this_date[11];
        message_date_key(&messages[candidate], this_date);

        /* Prepending a message costs as many rows as it wraps to.  It also
         * starts a date run (and therefore needs a divider) when it differs
         * from the next message.  Tracking the running row count makes this
         * newest-slice calculation O(n), rather than rescanning the whole
         * suffix for every candidate. */
        rows += history_view_message_lines(&messages[candidate], width);
        if (next_date[0] == '\0' || strcmp(this_date, next_date) != 0) {
            rows++;
        }
        if (rows > height) {
            break;
        }
        start = candidate;
        memcpy(next_date, this_date, sizeof(next_date));
    }

    if (start == count && count > 0) {
        start = count - 1;
    }
    return start;
}
