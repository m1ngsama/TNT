#include "support.h"
#include "support_text.h"

void support_append_interactive_panel(char *buffer, size_t buf_size,
                                      size_t *pos, help_lang_t lang) {
    if (!buffer || !pos) return;

    buffer_appendf(buffer, buf_size, pos, "%s",
                   support_text_interactive(lang));
}

void support_append_exec_panel(char *buffer, size_t buf_size, size_t *pos,
                               help_lang_t lang) {
    if (!buffer || !pos) return;

    buffer_appendf(buffer, buf_size, pos, "%s", support_text_exec(lang));
}
