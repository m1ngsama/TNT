#include "manual.h"
#include "manual_text.h"

void manual_append_interactive_panel(char *buffer, size_t buf_size,
                                     size_t *pos, help_lang_t lang) {
    if (!buffer || !pos) return;

    buffer_appendf(buffer, buf_size, pos, "%s",
                   manual_text_interactive(lang));
}
