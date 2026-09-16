#ifndef RICHTEXT_H
#define RICHTEXT_H

#include <stddef.h>

/* One display line, expressed as a byte range into the source text. */
typedef struct {
    size_t offset;
    size_t len;
} richtext_span_t;

/* Split text into display lines of at most `width` columns.  A newline is a
 * hard break.  Clusters are never split, so a line may be narrower than
 * `width` when the next cluster does not fit.  Returns the number of spans
 * written, which is 0 for NULL text, width < 1, or max_out == 0. */
size_t richtext_wrap(const char *text, int width, richtext_span_t *out,
                     size_t max_out);

typedef enum {
    RICHTEXT_PLAIN,
    RICHTEXT_BOLD,
    RICHTEXT_CODE,
    RICHTEXT_URL
} richtext_style_t;

/* One styled range of the visible text produced by richtext_parse. */
typedef struct {
    size_t offset;
    size_t len;
    richtext_style_t style;
} richtext_run_t;

#define RICHTEXT_MAX_RUNS 48

/* Strip the markers of the supported markdown subset out of `text` into
 * `out`, and describe what survived as styled runs over `out`.
 *
 * Wrapping and rendering both work on `out`, so the row count and the drawn
 * row can no longer disagree about where a line breaks.  Runs are ordered,
 * never overlap, and cover only the styled parts: the gaps between them are
 * plain.  Returns the number of runs written; `*out_len` receives the visible
 * length.  A marker that never closes stays literal.
 *
 * The subset is `**bold**`, `` `code` ``, ``` fences on their own line, and
 * bare http/https URLs.  Nesting is not parsed; code wins over bold. */
size_t richtext_parse(const char *text, char *out, size_t out_size,
                      size_t *out_len, richtext_run_t *runs, size_t max_runs);

/* SGR pair for a style.  `off` restores the surrounding text rather than
 * resetting it, so a run inside a highlighted message keeps the highlight. */
const char *richtext_style_on(richtext_style_t style);
const char *richtext_style_off(richtext_style_t style);

#endif /* RICHTEXT_H */
