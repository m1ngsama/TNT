#include "theme.h"

#include <stdio.h>
#include <string.h>

/* Accent slot helper: normal / bold / dim / italic variants of one ANSI
 * foreground colour code (30-37).  Kept in the 16-colour space for maximum
 * terminal compatibility. */
#define THEME_ACCENT(name, code)                 \
    {                                            \
        (name),                                  \
        "\033[" #code "m",                       \
        "\033[1;" #code "m",                     \
        "\033[2;" #code "m",                     \
        "\033[3;" #code "m"                       \
    }

static const theme_t g_themes[] = {
    THEME_ACCENT("cyan", 36),     /* default */
    THEME_ACCENT("green", 32),
    THEME_ACCENT("magenta", 35),
    THEME_ACCENT("blue", 34),
    THEME_ACCENT("amber", 33),
    THEME_ACCENT("red", 31),
    THEME_ACCENT("mono", 37),
};

#define THEME_COUNT (sizeof(g_themes) / sizeof(g_themes[0]))
#define THEME_DEFAULT_INDEX 0u

size_t theme_count(void) {
    return THEME_COUNT;
}

const theme_t *theme_at(size_t index) {
    if (index >= THEME_COUNT) {
        return NULL;
    }
    return &g_themes[index];
}

const theme_t *theme_find(const char *name) {
    if (!name || name[0] == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < THEME_COUNT; i++) {
        if (strcmp(g_themes[i].name, name) == 0) {
            return &g_themes[i];
        }
    }
    return NULL;
}

size_t theme_default_index(void) {
    return THEME_DEFAULT_INDEX;
}

const theme_t *theme_default(void) {
    return &g_themes[THEME_DEFAULT_INDEX];
}

const theme_t *theme_resolve(int index) {
    if (index < 0 || (size_t)index >= THEME_COUNT) {
        return theme_default();
    }
    return &g_themes[index];
}

void theme_append_names(char *buffer, size_t buf_size, size_t *pos) {
    if (!buffer || !pos || buf_size == 0) {
        return;
    }
    for (size_t i = 0; i < THEME_COUNT; i++) {
        const char *sep = (i > 0) ? ", " : "";
        int n = snprintf(buffer + *pos, buf_size - *pos, "%s%s", sep,
                         g_themes[i].name);
        if (n < 0 || (size_t)n >= buf_size - *pos) {
            buffer[buf_size - 1] = '\0';
            *pos = buf_size - 1;
            return;
        }
        *pos += (size_t)n;
    }
}
