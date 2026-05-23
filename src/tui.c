#include "tui.h"
#include "client.h"
#include "ssh_server.h"
#include "chat_room.h"
#include "history_view.h"
#include "i18n.h"
#include "tui_status.h"
#include "utf8.h"
#include <unistd.h>

static bool is_join_leave_msg(const message_t *msg) {
    if (strcmp(msg->username, "系统") != 0) return false;
    return strstr(msg->content, "加入了聊天室") != NULL ||
           strstr(msg->content, "离开了聊天室") != NULL;
}

static const char *username_color(const char *name) {
    static const char *colors[] = {
        "\033[31m", "\033[32m", "\033[33m",
        "\033[34m", "\033[35m", "\033[36m",
    };
    unsigned int h = 5381;
    for (const char *p = name; *p; p++)
        h = h * 33 + (unsigned char)*p;
    return colors[h % 6];
}

static void format_message_colored(const message_t *msg, char *buffer,
                                   size_t buf_size, int width,
                                   const char *my_username) {
    struct tm tm_info;
    localtime_r(&msg->timestamp, &tm_info);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M", &tm_info);

    /* Is this message from the local user?  Used to draw a 1-column gutter
     * marker so they can scan their own contributions when scrolling. */
    bool is_self = false;
    if (my_username && my_username[0] != '\0' &&
        strcmp(msg->username, "系统") != 0) {
        if (strcmp(msg->username, "*") == 0) {
            /* /me message: content starts with the actor's username */
            size_t un_len = strlen(my_username);
            if (strncmp(msg->content, my_username, un_len) == 0 &&
                (msg->content[un_len] == ' ' || msg->content[un_len] == '\0')) {
                is_self = true;
            }
        } else if (strcmp(msg->username, my_username) == 0) {
            is_self = true;
        }
    }
    /* Always 1 column wide so all messages align vertically. */
    const char *gutter = is_self ? "\033[36m▎\033[0m" : " ";

    bool mentioned = false;
    if (my_username && my_username[0] != '\0' &&
        strcmp(msg->username, "系统") != 0) {
        char mention[MAX_USERNAME_LEN + 2];
        snprintf(mention, sizeof(mention), "@%s", my_username);
        if (strstr(msg->content, mention) != NULL) {
            mentioned = true;
        }
    }
    const char *hl_start = mentioned ? "\033[1;33m" : "";
    const char *hl_end = mentioned ? "\033[0m" : "";

    if (strcmp(msg->username, "系统") == 0) {
        snprintf(buffer, buf_size,
                 "%s\033[90m--> %s\033[0m", gutter, msg->content);
    } else if (strcmp(msg->username, "*") == 0) {
        snprintf(buffer, buf_size,
                 "%s\033[90m%s\033[0m \033[3;36m* %s\033[0m",
                 gutter, time_str, msg->content);
    } else {
        snprintf(buffer, buf_size,
                 "%s\033[90m%s\033[0m %s%s\033[0m: %s%s%s",
                 gutter, time_str, username_color(msg->username),
                 msg->username, hl_start, msg->content, hl_end);
    }

    /* Plain-text version for width calculation — gutter is 1 column. */
    char plain[MAX_MESSAGE_LEN + 128];
    if (strcmp(msg->username, "系统") == 0) {
        snprintf(plain, sizeof(plain), " --> %s", msg->content);
    } else if (strcmp(msg->username, "*") == 0) {
        snprintf(plain, sizeof(plain), " %s * %s", time_str, msg->content);
    } else {
        snprintf(plain, sizeof(plain), " %s %s: %s",
                 time_str, msg->username, msg->content);
    }

    if (utf8_string_width(plain) > width) {
        /* Rebuild with truncated content — prefix_plain also includes the
         * 1-column gutter so the budget math comes out right. */
        int prefix_width;
        char prefix_plain[256];
        if (strcmp(msg->username, "系统") == 0) {
            snprintf(prefix_plain, sizeof(prefix_plain), " --> ");
        } else if (strcmp(msg->username, "*") == 0) {
            snprintf(prefix_plain, sizeof(prefix_plain), " %s * ", time_str);
        } else {
            snprintf(prefix_plain, sizeof(prefix_plain), " %s %s: ",
                     time_str, msg->username);
        }
        prefix_width = utf8_string_width(prefix_plain);
        int content_width = width - prefix_width;
        if (content_width < 4) content_width = 4;

        char truncated_content[MAX_MESSAGE_LEN];
        if (strcmp(msg->username, "系统") == 0) {
            strncpy(truncated_content, msg->content, sizeof(truncated_content) - 1);
            truncated_content[sizeof(truncated_content) - 1] = '\0';
        } else if (strcmp(msg->username, "*") == 0) {
            snprintf(truncated_content, sizeof(truncated_content), "* %s", msg->content);
        } else {
            strncpy(truncated_content, msg->content, sizeof(truncated_content) - 1);
            truncated_content[sizeof(truncated_content) - 1] = '\0';
        }
        utf8_truncate(truncated_content, content_width);

        if (strcmp(msg->username, "系统") == 0) {
            snprintf(buffer, buf_size,
                     "%s\033[90m--> %s\033[0m", gutter, truncated_content);
        } else if (strcmp(msg->username, "*") == 0) {
            snprintf(buffer, buf_size,
                     "%s\033[90m%s\033[0m \033[3;36m%s\033[0m",
                     gutter, time_str, truncated_content);
        } else {
            snprintf(buffer, buf_size,
                     "%s\033[90m%s\033[0m %s%s\033[0m: %s%s%s",
                     gutter, time_str, username_color(msg->username),
                     msg->username, hl_start, truncated_content, hl_end);
        }
    }
}

/* Clear the screen */
void tui_clear_screen(client_t *client) {
    if (!client || !client->connected) return;
    const char *clear = ANSI_CLEAR ANSI_HOME;
    client_send(client, clear, strlen(clear));
}

/* Render the pre-login welcome banner.
 *
 * Centred horizontally; vertically positioned about a third of the way down
 * the available height so the user's eye lands naturally on it before the
 * prompt below.  Uses light box-drawing characters (U+256D / U+2570) so the
 * frame matches the rest of the TUI's aesthetic instead of the older ASCII
 * `==` rules. */
void tui_render_welcome(client_t *client) {
    if (!client || !client->connected) return;

    int rw = client->width;
    int rh = client->height;
    if (rw < 10) rw = 10;
    if (rh < 4) rh = 4;

    /* Lines, in display order.  Width is computed in display columns. */
    const char *line1 = "TNT · " TNT_VERSION;
    const char *line2 = "匿名聊天室 · SSH";
    const char *line3 = "Anonymous chat over SSH";

    int inner_w = utf8_string_width(line1);
    int w2 = utf8_string_width(line2);
    int w3 = utf8_string_width(line3);
    if (w2 > inner_w) inner_w = w2;
    if (w3 > inner_w) inner_w = w3;
    inner_w += 4;  /* 2 columns padding on each side */

    /* Fall back to plain prompt if the terminal is too narrow for the frame. */
    if (inner_w + 2 > rw) {
        char fallback[128];
        int n = snprintf(fallback, sizeof(fallback),
                         ANSI_CLEAR ANSI_HOME
                         "TNT %s — anonymous chat over SSH\r\n\r\n",
                         TNT_VERSION);
        if (n > 0) client_send(client, fallback, (size_t)n);
        return;
    }

    int top_pad = rh / 3;
    if (top_pad < 1) top_pad = 1;
    int left_pad = (rw - (inner_w + 2)) / 2;
    if (left_pad < 0) left_pad = 0;

    /* ~5 KiB is plenty for the framed banner even on the largest terminals. */
    char buf[4096];
    size_t pos = 0;

    buffer_appendf(buf, sizeof(buf), &pos, ANSI_CLEAR ANSI_HOME);
    for (int i = 0; i < top_pad; i++) {
        buffer_appendf(buf, sizeof(buf), &pos, "\r\n");
    }

    /* Top border: ╭───…───╮ */
    for (int i = 0; i < left_pad; i++) buffer_append_bytes(buf, sizeof(buf), &pos, " ", 1);
    buffer_append_bytes(buf, sizeof(buf), &pos, "\033[36m", 5);
    buffer_append_bytes(buf, sizeof(buf), &pos, "╭", strlen("╭"));
    for (int i = 0; i < inner_w; i++) buffer_append_bytes(buf, sizeof(buf), &pos, "─", strlen("─"));
    buffer_append_bytes(buf, sizeof(buf), &pos, "╮", strlen("╮"));
    buffer_append_bytes(buf, sizeof(buf), &pos, "\033[0m", 4);
    buffer_appendf(buf, sizeof(buf), &pos, "\r\n");

    /* Three content lines with surrounding │ borders, centred inside the frame. */
    const char *lines[3] = {line1, line2, line3};
    int widths[3] = {utf8_string_width(line1), w2, w3};
    const char *line_color[3] = {"\033[1;36m", "\033[0m", "\033[2;37m"};
    for (int li = 0; li < 3; li++) {
        for (int i = 0; i < left_pad; i++) buffer_append_bytes(buf, sizeof(buf), &pos, " ", 1);
        buffer_append_bytes(buf, sizeof(buf), &pos, "\033[36m", 5);
        buffer_append_bytes(buf, sizeof(buf), &pos, "│", strlen("│"));
        buffer_append_bytes(buf, sizeof(buf), &pos, "\033[0m", 4);

        int pad_total = inner_w - widths[li];
        int pad_left = pad_total / 2;
        int pad_right = pad_total - pad_left;
        for (int i = 0; i < pad_left; i++) buffer_append_bytes(buf, sizeof(buf), &pos, " ", 1);
        buffer_appendf(buf, sizeof(buf), &pos, "%s%s\033[0m", line_color[li], lines[li]);
        for (int i = 0; i < pad_right; i++) buffer_append_bytes(buf, sizeof(buf), &pos, " ", 1);

        buffer_append_bytes(buf, sizeof(buf), &pos, "\033[36m", 5);
        buffer_append_bytes(buf, sizeof(buf), &pos, "│", strlen("│"));
        buffer_append_bytes(buf, sizeof(buf), &pos, "\033[0m", 4);
        buffer_appendf(buf, sizeof(buf), &pos, "\r\n");
    }

    /* Bottom border: ╰───…───╯ */
    for (int i = 0; i < left_pad; i++) buffer_append_bytes(buf, sizeof(buf), &pos, " ", 1);
    buffer_append_bytes(buf, sizeof(buf), &pos, "\033[36m", 5);
    buffer_append_bytes(buf, sizeof(buf), &pos, "╰", strlen("╰"));
    for (int i = 0; i < inner_w; i++) buffer_append_bytes(buf, sizeof(buf), &pos, "─", strlen("─"));
    buffer_append_bytes(buf, sizeof(buf), &pos, "╯", strlen("╯"));
    buffer_append_bytes(buf, sizeof(buf), &pos, "\033[0m", 4);
    buffer_appendf(buf, sizeof(buf), &pos, "\r\n\r\n");

    client_send(client, buf, pos);
}

/* Render the main screen */
void tui_render_screen(client_t *client) {
    if (!client || !client->connected) return;

    int render_width = client->width;
    int render_height = client->height;
    if (render_width < 10) render_width = 10;
    if (render_height < 4) render_height = 4;

    const size_t buf_size = (size_t)(render_height + 10) * (MAX_MESSAGE_LEN + 64) + 2048;
    char *buffer = malloc(buf_size);
    if (!buffer) return;
    size_t pos = 0;
    buffer[0] = '\0';

    /* First pass under lock: compute indices and counts */
    pthread_rwlock_rdlock(&g_room->lock);
    int online = g_room->client_count;
    int msg_count = g_room->message_count;
    pthread_rwlock_unlock(&g_room->lock);

    /* Calculate which messages to show.  The initial slice is capped by
     * message count; the lock-held copy below tightens "latest" slices so
     * date dividers cannot push the newest messages off-screen. */
    int msg_height = history_view_height(render_height);

    int start = 0;
    int latest_scroll_start = history_view_max_scroll(msg_count, msg_height);
    bool anchor_latest = client->mode != MODE_NORMAL ||
                         client->follow_tail ||
                         client->scroll_pos >= latest_scroll_start;
    if (client->mode == MODE_NORMAL) {
        start = client->scroll_pos;
        if (start > latest_scroll_start) {
            start = latest_scroll_start;
        }
        if (start < 0) start = 0;
    } else {
        /* INSERT mode: show latest */
        start = latest_scroll_start;
    }

    int end = start + msg_height;
    if (end > msg_count) end = msg_count;

    /* Allocate snapshot outside the lock to avoid blocking writers */
    message_t *msg_snapshot = NULL;
    int snapshot_capacity = msg_height;
    int snapshot_count = end - start;

    if (snapshot_count > 0 && snapshot_capacity > 0) {
        msg_snapshot = calloc(snapshot_capacity, sizeof(message_t));
    }

    /* Second pass under lock: copy messages */
    if (msg_snapshot) {
        pthread_rwlock_rdlock(&g_room->lock);
        /* Re-clamp in case msg_count changed */
        int actual_count = g_room->message_count;
        int actual_start = start;
        int actual_end = end;
        if (anchor_latest) {
            actual_end = actual_count;
            actual_start = history_view_latest_start_for_height(
                g_room->messages, actual_count, msg_height);
        } else {
            actual_end = (actual_end <= actual_count) ? actual_end : actual_count;
            actual_start = (actual_start < actual_end) ? actual_start : actual_end;
        }
        int actual_snapshot = actual_end - actual_start;
        if (actual_snapshot > 0 && actual_snapshot <= snapshot_capacity) {
            memcpy(msg_snapshot, &g_room->messages[actual_start],
                   actual_snapshot * sizeof(message_t));
            start = actual_start;
            end = actual_end;
            snapshot_count = actual_snapshot;
        } else {
            snapshot_count = 0;
        }
        pthread_rwlock_unlock(&g_room->lock);
    }

    /* Now render using snapshot (no lock held) */

    /* If mute_joins is set, remove join/leave messages from snapshot in place */
    if (client->mute_joins && msg_snapshot) {
        int filtered = 0;
        for (int i = 0; i < snapshot_count; i++) {
            if (!is_join_leave_msg(&msg_snapshot[i])) {
                msg_snapshot[filtered++] = msg_snapshot[i];
            }
        }
        snapshot_count = filtered;
    }

    /* Move to top (Home) - Do NOT clear screen to prevent flicker */
    buffer_appendf(buffer, buf_size, &pos, ANSI_HOME);

    /* Title bar — segmented chips on a single line, no full-line reverse.
     *
     * Segments (left to right), each followed by a dim middle-dot:
     *   • bold username
     *   • online count
     *   • mode name (colour matches the mode itself: cyan/yellow/magenta)
     *   • mute marker, only when active
     *   • right-aligned hint
     *
     * When the terminal is narrow, drop the optional segments in
     * reverse priority: hint → mute → mode chip → online count, until
     * what's left fits.  The bold username is always shown. */
    struct title_chip { const char *value; const char *value_color; };
    struct title_chip chips[3];
    int chip_count = 0;

    chips[chip_count].value = client->username;
    chips[chip_count].value_color = "\033[1;37m";
    chip_count++;

    char online_buf[32];
    snprintf(online_buf, sizeof(online_buf), "在线 %d", online);
    chips[chip_count].value = online_buf;
    chips[chip_count].value_color = "\033[37m";
    chip_count++;

    const char *mode_str;
    const char *mode_color;
    switch (client->mode) {
        case MODE_INSERT:  mode_str = "INSERT";  mode_color = "\033[36m"; break;
        case MODE_NORMAL:  mode_str = "NORMAL";  mode_color = "\033[33m"; break;
        case MODE_COMMAND: mode_str = "COMMAND"; mode_color = "\033[35m"; break;
        default:           mode_str = "HELP";    mode_color = "\033[34m"; break;
    }
    chips[chip_count].value = mode_str;
    chips[chip_count].value_color = mode_color;
    chip_count++;

    const char *hint = "? 帮助";
    int hint_width = utf8_string_width(hint);
    int mute_width = client->mute_joins ? 6 : 0;  /* "  静音" = 2 + 4 */

    /* Unread @-mentions chip — high-priority, gets a bright yellow star.
     * Sits between mode and hint when present, and survives degradation
     * longer than the hint / mute / mode chips. */
    int unread_count = client->unread_mentions;
    char unread_buf[32] = "";
    int unread_width = 0;
    if (unread_count > 0) {
        snprintf(unread_buf, sizeof(unread_buf), "★ %d", unread_count);
        unread_width = utf8_string_width(unread_buf) + 2;  /* leading " · " minus initial space accounted later */
    }

    /* Unread whispers chip — bright magenta envelope.  Same priority as
     * the mentions chip; both signal "you missed something". */
    int whisper_count = client->unread_whispers;
    char whisper_buf[32] = "";
    int whisper_width = 0;
    if (whisper_count > 0) {
        snprintf(whisper_buf, sizeof(whisper_buf), "✉ %d", whisper_count);
        whisper_width = utf8_string_width(whisper_buf) + 2;
    }

    /* Decide what fits.  Reserve at least 1 col of gap between left and
     * right halves so they never visually touch. */
    int show_hint = 1;
    int show_mute = client->mute_joins ? 1 : 0;
    int show_unread = unread_count > 0 ? 1 : 0;
    int show_whisper = whisper_count > 0 ? 1 : 0;
    int show_chips = chip_count;

    while (show_chips > 1) {
        int left_w = 1 /*leading space*/;
        for (int i = 0; i < show_chips; i++) {
            if (i > 0) left_w += 3;  /* " · " */
            left_w += utf8_string_width(chips[i].value);
        }
        if (show_mute) left_w += mute_width;
        if (show_unread) left_w += unread_width + 1;
        if (show_whisper) left_w += whisper_width + 1;
        int right_w = (show_hint ? hint_width + 1 /*trailing space*/ : 0);
        int needed = left_w + 1 /*min gap*/ + right_w;
        if (needed <= render_width) break;

        /* Drop priority: hint → mute → mode → online → whispers → mentions. */
        if (show_hint)         { show_hint = 0; continue; }
        if (show_mute)         { show_mute = 0; continue; }
        if (show_chips > 1)    { show_chips--;  continue; }
        if (show_whisper)      { show_whisper = 0; continue; }
        if (show_unread)       { show_unread = 0; continue; }
        break;
    }

    /* Compose left half. */
    char left[256];
    size_t lpos = 0;
    int left_width = 0;
    for (int i = 0; i < show_chips; i++) {
        if (i > 0) {
            buffer_appendf(left, sizeof(left), &lpos, "\033[2;37m · \033[0m");
            left_width += 3;
        }
        buffer_appendf(left, sizeof(left), &lpos, "%s%s\033[0m",
                       chips[i].value_color, chips[i].value);
        left_width += utf8_string_width(chips[i].value);
    }
    if (show_mute) {
        buffer_appendf(left, sizeof(left), &lpos, "  \033[2;37m静音\033[0m");
        left_width += mute_width;
    }
    if (show_unread) {
        buffer_appendf(left, sizeof(left), &lpos,
                       "  \033[1;33m%s\033[0m", unread_buf);
        left_width += unread_width + 1;
    }
    if (show_whisper) {
        buffer_appendf(left, sizeof(left), &lpos,
                       "  \033[1;35m%s\033[0m", whisper_buf);
        left_width += whisper_width + 1;
    }

    int gap = render_width - left_width - (show_hint ? hint_width + 2 : 1);
    if (gap < 1) gap = 1;

    buffer_appendf(buffer, buf_size, &pos, " %s", left);
    for (int i = 0; i < gap; i++) {
        buffer_append_bytes(buffer, buf_size, &pos, " ", 1);
    }
    if (show_hint) {
        buffer_appendf(buffer, buf_size, &pos,
                       "\033[2;37m%s\033[0m \033[K\r\n", hint);
    } else {
        buffer_appendf(buffer, buf_size, &pos, "\033[K\r\n");
    }

    /* Render messages from snapshot.  Insert a dim "── YYYY-MM-DD ──" divider
     * before the first message of each new day so the eye can land on dates
     * when scrolling through a long-lived room.
     *
     * We track rows_written separately from snapshot index so dividers
     * compete with messages for the fixed message-area height — they do not
     * push other content off the bottom. */
    int rows_written = 0;
    if (msg_snapshot) {
        char last_date[11] = "";  /* "YYYY-MM-DD" */
        for (int i = 0; i < snapshot_count && rows_written < msg_height; i++) {
            char this_date[11];
            struct tm tmi;
            localtime_r(&msg_snapshot[i].timestamp, &tmi);
            strftime(this_date, sizeof(this_date), "%Y-%m-%d", &tmi);

            if (strcmp(this_date, last_date) != 0) {
                /* Build divider: "── YYYY-MM-DD " then fill the rest with ─ */
                int prefix_w = 3 + 10 + 1;  /* "── 2026-05-17 " in display columns */
                int dash_fill = render_width - prefix_w;
                if (dash_fill < 0) dash_fill = 0;

                buffer_appendf(buffer, buf_size, &pos, "\033[2;37m── %s ", this_date);
                for (int j = 0; j < dash_fill; j++) {
                    buffer_append_bytes(buffer, buf_size, &pos, "─", strlen("─"));
                }
                buffer_appendf(buffer, buf_size, &pos, "\033[0m\033[K\r\n");

                memcpy(last_date, this_date, sizeof(last_date));
                rows_written++;
                if (rows_written >= msg_height) break;
            }

            char msg_line[2048];
            format_message_colored(&msg_snapshot[i], msg_line, sizeof(msg_line),
                                   render_width, client->username);
            buffer_appendf(buffer, buf_size, &pos, "%s\033[K\r\n", msg_line);
            rows_written++;
        }
        free(msg_snapshot);
    }

    /* Fill empty lines and clear them */
    for (int i = rows_written; i < msg_height; i++) {
        buffer_appendf(buffer, buf_size, &pos, "\033[K\r\n");
    }

    /* Separator - use box drawing character */
    for (int i = 0; i < render_width; i++) {
        buffer_append_bytes(buffer, buf_size, &pos, "─", strlen("─"));
    }
    buffer_appendf(buffer, buf_size, &pos, "\033[K\r\n");

    /* Status/Input line */
    tui_status_append(buffer, buf_size, &pos, client, msg_count, start, end);

    client_send(client, buffer, pos);
    free(buffer);
}

/* Render the input line.
 *
 * Format: "› <input>"  with optional right-aligned length indicator
 * once the buffer is past 80% full.  The indicator turns bold-yellow
 * past 95% so users can see further keystrokes will be dropped. */
void tui_render_input(client_t *client, const char *input) {
    if (!client || !client->connected) return;

    int rw = client->width;
    int rh = client->height;
    if (rw < 10) rw = 10;
    if (rh < 4) rh = 4;

    char buffer[2048];
    int input_width = utf8_string_width(input);
    size_t input_bytes = strlen(input);

    /* Decide whether to show the length gauge and how loud. */
    int gauge_width = 0;
    char gauge[64] = "";
    if (input_bytes > (MAX_MESSAGE_LEN * 8) / 10) {  /* > 80 % */
        size_t remaining = (input_bytes < MAX_MESSAGE_LEN)
                           ? (MAX_MESSAGE_LEN - 1 - input_bytes) : 0;
        const char *color =
            (input_bytes > (MAX_MESSAGE_LEN * 95) / 100) ? "\033[1;33m"
                                                          : "\033[2;37m";
        snprintf(gauge, sizeof(gauge), "%s… %zu B\033[0m", color, remaining);
        /* Plain-text width: " … 1234 B" → 4 + len(digits) + 2 */
        char digits[12];
        snprintf(digits, sizeof(digits), "%zu", remaining);
        gauge_width = 4 + (int)strlen(digits) + 2;  /* "… ", digits, " B" + leading space */
    }

    int avail = rw - 3 - (gauge_width > 0 ? gauge_width + 1 : 0);
    if (avail < 1) avail = 1;

    /* Truncate from start if too long */
    char display[MAX_MESSAGE_LEN];
    strncpy(display, input, sizeof(display) - 1);
    display[sizeof(display) - 1] = '\0';

    if (input_width > avail) {
        int excess = input_width - avail;
        int skip_width = 0;
        const char *p = input;
        int bytes_read;

        while (*p && skip_width < excess) {
            uint32_t cp = utf8_decode(p, &bytes_read);
            skip_width += utf8_char_width(cp);
            p += bytes_read;
        }

        strncpy(display, p, sizeof(display) - 1);
    }

    /* Compose: cursor to input row, clear line, "› " prompt, input.
     * If a gauge is active, append it right-aligned. */
    if (gauge_width > 0) {
        int displayed_width = utf8_string_width(display);
        int padding = rw - 2 - displayed_width - gauge_width;
        if (padding < 1) padding = 1;
        snprintf(buffer, sizeof(buffer),
                 "\033[%d;1H" ANSI_CLEAR_LINE "\033[2;37m›\033[0m %s%*s%s",
                 rh, display, padding, "", gauge);
    } else {
        snprintf(buffer, sizeof(buffer),
                 "\033[%d;1H" ANSI_CLEAR_LINE "\033[2;37m›\033[0m %s",
                 rh, display);
    }

    client_send(client, buffer, strlen(buffer));
}

/* Render the command output screen */
void tui_render_command_output(client_t *client) {
    if (!client || !client->connected) return;

    int rw = client->width;
    int rh = client->height;
    if (rw < 10) rw = 10;
    if (rh < 4) rh = 4;

    char buffer[4096];
    size_t pos = 0;
    buffer[0] = '\0';

    /* Clear screen */
    buffer_appendf(buffer, sizeof(buffer), &pos, ANSI_CLEAR ANSI_HOME);

    /* Title */
    const char *title = " COMMAND OUTPUT ";
    char title_display[64];
    utf8_ansi_truncate(title, title_display, sizeof(title_display), rw);
    int title_width = utf8_ansi_string_width(title_display);
    int padding = rw - title_width;
    if (padding < 0) padding = 0;

    buffer_appendf(buffer, sizeof(buffer), &pos, ANSI_REVERSE "%s",
                   title_display);
    for (int i = 0; i < padding; i++) {
        buffer_append_bytes(buffer, sizeof(buffer), &pos, " ", 1);
    }
    buffer_appendf(buffer, sizeof(buffer), &pos, ANSI_RESET "\r\n");

    /* Command output - use a copy to avoid strtok corruption */
    char output_copy[2048];
    strncpy(output_copy, client->command_output, sizeof(output_copy) - 1);
    output_copy[sizeof(output_copy) - 1] = '\0';

    char *line = strtok(output_copy, "\n");
    int line_count = 0;
    int max_lines = rh - 2;

    while (line && line_count < max_lines) {
        char truncated[1024];
        utf8_ansi_truncate(line, truncated, sizeof(truncated), rw);

        buffer_appendf(buffer, sizeof(buffer), &pos, "%s\r\n", truncated);
        line = strtok(NULL, "\n");
        line_count++;
    }

    client_send(client, buffer, pos);
}

/* Render the MOTD screen.
 *
 * A framed banner with a title chip embedded in the top border and an
 * "any key to continue" hint embedded in the bottom border, MOTD body
 * left-padded inside.  Dismissed by handle_key like any other modal
 * (sets command_output[0]='\0' and show_motd=false).
 *
 * Lighter aesthetic than tui_render_command_output: no full-line reverse,
 * dim borders, two blank lines of breathing room above and below the
 * body so the announcement reads as a notice rather than a console dump. */
void tui_render_motd(client_t *client) {
    if (!client || !client->connected) return;

    int rw = client->width;
    int rh = client->height;
    if (rw < 10) rw = 10;
    if (rh < 4) rh = 4;

    char buffer[4096];
    size_t pos = 0;
    buffer_appendf(buffer, sizeof(buffer), &pos, ANSI_CLEAR ANSI_HOME);

    /* Top border: ╭─ 公告 / MOTD ──...──╮ */
    const char *title = " 公告 / MOTD ";
    int title_w = utf8_string_width(title);
    int top_dash_fill = rw - 2 - title_w - 1;  /* 2 corners, 1 leading ─ */
    if (top_dash_fill < 0) top_dash_fill = 0;

    buffer_appendf(buffer, sizeof(buffer), &pos, "\033[2;36m╭─");
    buffer_appendf(buffer, sizeof(buffer), &pos, "\033[0;1;36m%s\033[2;36m", title);
    for (int i = 0; i < top_dash_fill; i++) {
        buffer_append_bytes(buffer, sizeof(buffer), &pos, "─", strlen("─"));
    }
    buffer_appendf(buffer, sizeof(buffer), &pos, "╮\033[0m\r\n");

    /* Top breathing-room line */
    buffer_appendf(buffer, sizeof(buffer), &pos, "\r\n");

    /* Body lines (left-pad 2 cols, truncate to inner width) */
    char body_copy[2048];
    strncpy(body_copy, client->command_output, sizeof(body_copy) - 1);
    body_copy[sizeof(body_copy) - 1] = '\0';

    int body_lines = 0;
    int max_body_lines = rh - 4;  /* top border + top pad + bottom pad + bottom border */
    if (max_body_lines < 1) max_body_lines = 1;

    char *line = strtok(body_copy, "\n");
    while (line && body_lines < max_body_lines) {
        char truncated[1024];
        strncpy(truncated, line, sizeof(truncated) - 1);
        truncated[sizeof(truncated) - 1] = '\0';

        int avail = rw - 4;  /* 2 cols padding each side */
        if (avail < 4) avail = 4;
        if (utf8_string_width(truncated) > avail) {
            utf8_truncate(truncated, avail);
        }
        buffer_appendf(buffer, sizeof(buffer), &pos, "  %s\r\n", truncated);
        body_lines++;
        line = strtok(NULL, "\n");
    }

    /* Fill empty space up to the bottom border */
    int used_rows = 1 /*top*/ + 1 /*pad*/ + body_lines + 1 /*pad*/ + 1 /*bottom*/;
    int filler_rows = rh - used_rows;
    if (filler_rows < 0) filler_rows = 0;
    for (int i = 0; i < filler_rows; i++) {
        buffer_appendf(buffer, sizeof(buffer), &pos, "\r\n");
    }

    /* Bottom breathing-room line */
    buffer_appendf(buffer, sizeof(buffer), &pos, "\r\n");

    /* Bottom border: ╰─ 按任意键继续 ─...─╯ */
    const char *footer = " 按任意键继续 / press any key ";
    int footer_w = utf8_string_width(footer);
    int bot_dash_fill = rw - 2 - footer_w - 1;
    if (bot_dash_fill < 0) bot_dash_fill = 0;

    buffer_appendf(buffer, sizeof(buffer), &pos, "\033[2;36m╰─");
    buffer_appendf(buffer, sizeof(buffer), &pos, "\033[0;2;37m%s\033[2;36m", footer);
    for (int i = 0; i < bot_dash_fill; i++) {
        buffer_append_bytes(buffer, sizeof(buffer), &pos, "─", strlen("─"));
    }
    buffer_appendf(buffer, sizeof(buffer), &pos, "╯\033[0m");

    client_send(client, buffer, pos);
}
const char* tui_get_help_text(help_lang_t lang) {
    if (lang == LANG_EN) {
        return "TERMINAL CHAT ROOM - HELP\n"
               "\n"
               "OPERATING MODES:\n"
               "  INSERT  - Type and send messages (default)\n"
               "  NORMAL  - Browse message history\n"
               "  COMMAND - Execute commands\n"
               "\n"
               "INSERT MODE KEYS:\n"
               "  ESC        - Enter NORMAL mode\n"
               "  Enter      - Send message\n"
               "  Backspace  - Delete character\n"
               "  Ctrl+W     - Delete last word\n"
               "  Ctrl+U     - Delete line\n"
               "  Ctrl+C     - Enter NORMAL mode\n"
               "\n"
               "NORMAL MODE KEYS:\n"
               "  Opens at latest messages\n"
               "  Follows latest until you scroll up\n"
               "  i          - Return to INSERT mode\n"
               "  :          - Enter COMMAND mode\n"
               "  j/k        - Scroll down/up one line\n"
               "  Ctrl+D/U   - Scroll half page down/up\n"
               "  Ctrl+F/B   - Scroll full page down/up\n"
               "  PgDn/PgUp  - Scroll full page down/up\n"
               "  End/Home   - Jump to bottom/top\n"
               "  g/G        - Jump to top/bottom\n"
               "  ?          - Show this help\n"
               "  Ctrl+C     - Exit chat\n"
               "\n"
               "AVAILABLE COMMANDS:\n"
               "  :list, :users        - Show online users\n"
               "  :nick <name>         - Change nickname\n"
               "  :msg <user> <text>   - Whisper to user\n"
               "  :w <user> <text>     - Short alias for :msg\n"
               "  :last [N]            - Show last N messages (max 50)\n"
               "  :search <keyword>    - Search message history\n"
               "  :mute-joins          - Toggle join/leave notices\n"
               "  :support             - Show quick support guide\n"
               "  :lang <en|zh>        - Switch UI language\n"
               "  :help                - Show available commands\n"
               "  :clear               - Clear command output\n"
               "  :q, :quit, :exit     - Disconnect\n"
               "\n"
               "SPECIAL MESSAGES:\n"
               "  /me <action>      - Send action (e.g. /me waves)\n"
               "  @username         - Mention user (bell + highlight)\n"
               "\n"
               "HELP SCREEN KEYS:\n"
               "  q, ESC     - Close help\n"
               "  j/k        - Scroll down/up\n"
               "  Ctrl+D/U   - Scroll half page down/up\n"
               "  Ctrl+F/B   - Scroll full page down/up\n"
               "  g/G        - Jump to top/bottom\n"
               "  e/z        - Switch English/Chinese\n";
    } else {
        return "终端聊天室 - 帮助\n"
               "\n"
               "操作模式:\n"
               "  INSERT  - 输入和发送消息(默认)\n"
               "  NORMAL  - 浏览消息历史\n"
               "  COMMAND - 执行命令\n"
               "\n"
               "INSERT 模式按键:\n"
               "  ESC        - 进入 NORMAL 模式\n"
               "  Enter      - 发送消息\n"
               "  Backspace  - 删除字符\n"
               "  Ctrl+W     - 删除上个单词\n"
               "  Ctrl+U     - 删除整行\n"
               "  Ctrl+C     - 进入 NORMAL 模式\n"
               "\n"
               "NORMAL 模式按键:\n"
               "  默认停在最新消息\n"
               "  未向上翻阅时自动跟随最新消息\n"
               "  i          - 返回 INSERT 模式\n"
               "  :          - 进入 COMMAND 模式\n"
               "  j/k        - 向下/上滚动一行\n"
               "  Ctrl+D/U   - 向下/上滚动半页\n"
               "  Ctrl+F/B   - 向下/上滚动整页\n"
               "  PgDn/PgUp  - 向下/上滚动整页\n"
               "  End/Home   - 跳到底部/顶部\n"
               "  g/G        - 跳到顶部/底部\n"
               "  ?          - 显示此帮助\n"
               "  Ctrl+C     - 退出聊天\n"
               "\n"
               "可用命令:\n"
               "  :list, :users        - 显示在线用户\n"
               "  :nick <名字>         - 更改昵称\n"
               "  :msg <用户> <文本>   - 私聊\n"
               "  :w <用户> <文本>     - :msg 的简写\n"
               "  :last [N]            - 显示最后 N 条消息(最多50)\n"
               "  :search <关键词>     - 搜索消息历史\n"
               "  :mute-joins          - 切换加入/离开提示\n"
               "  :support             - 显示快速支持指南\n"
               "  :lang <en|zh>        - 切换界面语言\n"
               "  :help                - 显示可用命令\n"
               "  :clear               - 清空命令输出\n"
               "  :q, :quit, :exit     - 断开连接\n"
               "\n"
               "特殊消息:\n"
               "  /me <动作>        - 发送动作 (如 /me 挥手)\n"
               "  @用户名           - 提及用户 (响铃+高亮)\n"
               "\n"
               "帮助界面按键:\n"
               "  q, ESC     - 关闭帮助\n"
               "  j/k        - 向下/上滚动\n"
               "  Ctrl+D/U   - 向下/上滚动半页\n"
               "  Ctrl+F/B   - 向下/上滚动整页\n"
               "  g/G        - 跳到顶部/底部\n"
               "  e/z        - 切换英文/中文\n";
    }
}

/* Render the help screen */
void tui_render_help(client_t *client) {
    if (!client || !client->connected) return;

    int rw = client->width;
    int rh = client->height;
    if (rw < 10) rw = 10;
    if (rh < 4) rh = 4;

    char buffer[8192];
    size_t pos = 0;
    buffer[0] = '\0';

    /* Clear screen */
    buffer_appendf(buffer, sizeof(buffer), &pos, ANSI_CLEAR ANSI_HOME);

    /* Title */
    const char *title = i18n_text(client->help_lang, I18N_HELP_TITLE);
    int title_width = utf8_string_width(title);
    int padding = rw - title_width;
    if (padding < 0) padding = 0;

    buffer_appendf(buffer, sizeof(buffer), &pos, ANSI_REVERSE "%s", title);
    for (int i = 0; i < padding; i++) {
        buffer_append_bytes(buffer, sizeof(buffer), &pos, " ", 1);
    }
    buffer_appendf(buffer, sizeof(buffer), &pos, ANSI_RESET "\r\n");

    /* Help content */
    const char *help_text = tui_get_help_text(client->help_lang);
    char help_copy[8192];
    strncpy(help_copy, help_text, sizeof(help_copy) - 1);
    help_copy[sizeof(help_copy) - 1] = '\0';

    /* Split into lines and display with scrolling */
    char *lines[100];
    int line_count = 0;
    char *line = strtok(help_copy, "\n");
    while (line && line_count < 100) {
        lines[line_count++] = line;
        line = strtok(NULL, "\n");
    }

    int content_height = rh - 2;
    if (content_height < 1) content_height = 1;
    int max_scroll = line_count - content_height + 1;
    if (max_scroll < 0) max_scroll = 0;
    int start = client->help_scroll_pos;
    if (start > max_scroll) start = max_scroll;
    int end = start + content_height - 1;
    if (end > line_count) end = line_count;

    for (int i = start; i < end && (i - start) < content_height - 1; i++) {
        buffer_appendf(buffer, sizeof(buffer), &pos, "%s\r\n", lines[i]);
    }

    /* Fill remaining lines */
    for (int i = end - start; i < content_height - 1; i++) {
        buffer_append_bytes(buffer, sizeof(buffer), &pos, "\r\n", 2);
    }

    /* Status line */
    buffer_appendf(buffer, sizeof(buffer), &pos,
                   i18n_text(client->help_lang, I18N_HELP_STATUS_FORMAT),
                   start + 1, max_scroll + 1);

    client_send(client, buffer, pos);
}
