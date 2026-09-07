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

#endif /* RICHTEXT_H */
