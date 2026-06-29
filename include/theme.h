#ifndef THEME_H
#define THEME_H

#include <stddef.h>

/* Per-session colour themes.
 *
 * A theme is a small set of semantic accent slots that the renderer
 * references instead of hardcoding SGR sequences.  Themes stay within the
 * portable 16-colour ANSI space so they render consistently on any terminal;
 * true colour is intentionally avoided so hierarchy never depends on it.
 *
 * Themes are a pure, per-client personalization: they change only what the
 * choosing user sees and never affect other users or server state.  This
 * module is free of client/SSH dependencies so it can be unit-tested alone. */
typedef struct {
    const char *name;          /* canonical lowercase name */
    const char *accent;        /* primary accent, e.g. "\033[36m" */
    const char *accent_bold;   /* bold accent, e.g. "\033[1;36m" */
    const char *accent_dim;    /* dim accent, e.g. "\033[2;36m" */
    const char *accent_italic; /* italic accent, e.g. "\033[3;36m" */
} theme_t;

/* Number of registered themes. */
size_t theme_count(void);

/* Theme at `index`, or NULL when out of range. */
const theme_t *theme_at(size_t index);

/* Theme whose name matches `name` (case-sensitive, lowercase), or NULL. */
const theme_t *theme_find(const char *name);

/* Index of the default theme. */
size_t theme_default_index(void);

/* The default theme (never NULL). */
const theme_t *theme_default(void);

/* Resolve a stored client theme index to a theme, falling back to the
 * default when the index is out of range.  Never returns NULL. */
const theme_t *theme_resolve(int index);

/* Append a comma-separated list of theme names into `buffer`. */
void theme_append_names(char *buffer, size_t buf_size, size_t *pos);

#endif /* THEME_H */
