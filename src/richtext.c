#include "richtext.h"

#include "utf8.h"

#include <string.h>

size_t richtext_wrap(const char *text, int width, richtext_span_t *out,
                     size_t max_out) {
    size_t line_start = 0;
    size_t offset = 0;
    size_t count = 0;
    size_t last_break = 0;      /* byte offset just past the last space */
    int line_width = 0;

    if (!text || !out || width < 1 || max_out == 0) {
        return 0;
    }

    while (text[offset] != '\0' && count < max_out) {
        size_t len;
        int cluster_width;

        if (text[offset] == '\n') {
            out[count].offset = line_start;
            out[count].len = offset - line_start;
            count++;
            offset++;
            line_start = offset;
            last_break = 0;
            line_width = 0;
            continue;
        }

        len = utf8_cluster_length(text + offset);
        if (len == 0) {
            break;
        }
        cluster_width = utf8_cluster_width(text + offset);

        if (line_width + cluster_width > width && offset > line_start) {
            size_t line_end = (last_break > line_start) ? last_break : offset;

            out[count].offset = line_start;
            out[count].len = line_end - line_start;
            /* Drop the space that caused the break. */
            while (out[count].len > 0 &&
                   text[out[count].offset + out[count].len - 1] == ' ') {
                out[count].len--;
            }
            count++;

            line_start = line_end;
            while (text[line_start] == ' ') {
                line_start++;
            }
            offset = line_start;
            last_break = 0;
            line_width = 0;
            continue;
        }

        offset += len;
        line_width += cluster_width;
        if (text[offset - len] == ' ') {
            last_break = offset;
        }
    }

    if (count < max_out && offset > line_start) {
        out[count].offset = line_start;
        out[count].len = offset - line_start;
        count++;
    } else if (count == 0 && count < max_out) {
        out[count].offset = 0;
        out[count].len = 0;
        count++;
    }

    return count;
}
