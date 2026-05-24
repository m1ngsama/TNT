#include "tui_status.h"
#include "i18n.h"
#include "ssh_server.h"

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
        buffer_appendf(buffer, buf_size, pos,
                       "\033[35m:\033[0m%s\033[K", client->command_input);
    }
}
