#include "tui_status.h"
#include "i18n.h"
#include "ssh_server.h"
#include "utf8.h"

static void format_command_input_tail(const char *input, int avail_width,
                                      char *display, size_t display_size) {
    if (!input || !display || display_size == 0) return;

    display[0] = '\0';
    if (avail_width < 1) {
        return;
    }

    if (utf8_string_width(input) <= avail_width) {
        strncpy(display, input, display_size - 1);
        display[display_size - 1] = '\0';
        return;
    }

    const char *marker = "<";
    int marker_width = 1;
    int tail_width = avail_width - marker_width;
    if (tail_width < 1) {
        snprintf(display, display_size, "%s", marker);
        return;
    }

    const char *p = input + strlen(input);
    const char *tail = p;
    int width = 0;

    while (p > input && width < tail_width) {
        const char *q = p - 1;
        while (q > input && ((*q & 0xC0) == 0x80)) {
            q--;
        }

        int bytes_read = 0;
        uint32_t cp = utf8_decode(q, &bytes_read);
        int char_width = utf8_char_width(cp);
        if (width + char_width > tail_width) {
            break;
        }
        width += char_width;
        tail = q;
        p = q;
    }

    snprintf(display, display_size, "%s%s", marker, tail);
}

void tui_status_append(char *buffer, size_t buf_size, size_t *pos,
                       const struct client *client, int msg_count,
                       int start, int end) {
    if (!buffer || !pos || !client) return;

    if (client->mode == MODE_INSERT) {
        if (client->width >= 58) {
            buffer_appendf(buffer, buf_size, pos,
                           "\033[2;37m›\033[0m  "
                           "\033[2;37m%s\033[0m"
                           "\033[K",
                           i18n_text(client->ui_lang,
                                     I18N_INSERT_HINT_WIDE));
        } else if (client->width >= 36) {
            buffer_appendf(buffer, buf_size, pos,
                           "\033[2;37m›\033[0m  "
                           "\033[2;37m%s\033[0m\033[K",
                           i18n_text(client->ui_lang,
                                     I18N_INSERT_HINT_NARROW));
        } else {
            buffer_appendf(buffer, buf_size, pos, "\033[2;37m›\033[0m \033[K");
        }
    } else if (client->mode == MODE_NORMAL) {
        int total = msg_count;
        int range_start = total == 0 ? 0 : start + 1;
        int range_end = total == 0 ? 0 : end;
        int unseen = msg_count - end;

        if (unseen > 0) {
            buffer_appendf(buffer, buf_size, pos,
                           "\033[7;33m NORMAL \033[0m"
                           "  \033[2;37m%d-%d / %d\033[0m"
                           "   \033[33m▼ %d %s · %s\033[0m\033[K",
                           range_start, range_end, total, unseen,
                           i18n_text(client->ui_lang,
                                     I18N_NORMAL_NEW_MESSAGES),
                           i18n_text(client->ui_lang, I18N_NORMAL_LATEST));
        } else {
            buffer_appendf(buffer, buf_size, pos,
                           "\033[7;33m NORMAL \033[0m"
                           "  \033[2;37m%d-%d / %d\033[0m"
                           "   \033[2;37m%s\033[0m\033[K",
                           range_start, range_end, total,
                           i18n_text(client->ui_lang, I18N_NORMAL_LATEST));
        }
    } else if (client->mode == MODE_COMMAND) {
        char display[sizeof(client->command_input) + 2];
        int avail = client->width - 1;
        if (avail < 1) avail = 1;
        format_command_input_tail(client->command_input, avail, display,
                                  sizeof(display));
        buffer_appendf(buffer, buf_size, pos,
                       "\033[35m:\033[0m%s\033[K", display);
    }
}
