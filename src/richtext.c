#include "richtext.h"

#include "utf8.h"

#include <stdbool.h>
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

static bool emit(char *out, size_t out_size, size_t *oi,
                 const char *src, size_t n) {
    if (*oi + n >= out_size) {
        n = (*oi < out_size - 1) ? (out_size - 1 - *oi) : 0;
    }
    if (n == 0) {
        return false;
    }
    memcpy(out + *oi, src, n);
    *oi += n;
    return true;
}

/* Counts against max_runs even when the caller wants no run list, so a caller
 * measuring the text and a caller drawing it agree on what a full table does. */
static void add_run(richtext_run_t *runs, size_t max_runs, size_t *rc,
                    size_t offset, size_t len, richtext_style_t style) {
    if (*rc >= max_runs || len == 0) {
        return;
    }
    if (runs) {
        runs[*rc].offset = offset;
        runs[*rc].len = len;
        runs[*rc].style = style;
    }
    (*rc)++;
}

static size_t line_length(const char *p) {
    const char *nl = strchr(p, '\n');
    return nl ? (size_t)(nl - p) : strlen(p);
}

/* A fence is its own line and nothing else, so it can never be confused with
 * three backticks used inside a sentence. */
static bool is_fence(const char *line_start, size_t len) {
    size_t i;

    if (len < 3 || strncmp(line_start, "```", 3) != 0) {
        return false;
    }
    for (i = 3; i < len; i++) {
        if (line_start[i] != ' ' && line_start[i] != '\t') {
            return false;
        }
    }
    return true;
}

static const char *find_closing_fence(const char *p) {
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);

        if (is_fence(p, len)) {
            return p;
        }
        if (!nl) {
            return NULL;
        }
        p = nl + 1;
    }
    return NULL;
}

static bool url_starts_here(const char *p) {
    return strncmp(p, "http://", 7) == 0 || strncmp(p, "https://", 8) == 0;
}

static size_t url_length(const char *p) {
    size_t n = 0;

    while (p[n] && p[n] != ' ' && p[n] != '\n' && p[n] != '\t') {
        n++;
    }
    while (n > 0 && strchr(".,;:!?)", p[n - 1]) != NULL) {
        n--;
    }
    return n;
}

/* Inline markers never span a line, so every search stops at the newline. */
static const char *find_in_line(const char *p, const char *needle) {
    size_t n = strlen(needle);

    for (; *p && *p != '\n'; p++) {
        if (strncmp(p, needle, n) == 0) {
            return p;
        }
    }
    return NULL;
}

static void parse_inline(const char *p, const char *end, char *out,
                         size_t out_size, size_t *oi, richtext_run_t *runs,
                         size_t max_runs, size_t *rc) {
    while (p < end && *p) {
        if (*p == '`') {
            const char *close = find_in_line(p + 1, "`");
            if (close && close < end && close > p + 1 && *rc < max_runs) {
                size_t start = *oi;
                emit(out, out_size, oi, p + 1, (size_t)(close - p - 1));
                add_run(runs, max_runs, rc, start, *oi - start, RICHTEXT_CODE);
                p = close + 1;
                continue;
            }
        } else if (p[0] == '*' && p[1] == '*') {
            const char *close = find_in_line(p + 2, "**");
            if (close && close < end && close > p + 2 && *rc < max_runs) {
                size_t start = *oi;
                emit(out, out_size, oi, p + 2, (size_t)(close - p - 2));
                add_run(runs, max_runs, rc, start, *oi - start, RICHTEXT_BOLD);
                p = close + 2;
                continue;
            }
        } else if (url_starts_here(p) &&
                   (*oi == 0 || out[*oi - 1] == ' ' || out[*oi - 1] == '\n')) {
            size_t n = url_length(p);
            if (n > 0 && *rc < max_runs) {
                size_t start = *oi;
                emit(out, out_size, oi, p, n);
                add_run(runs, max_runs, rc, start, *oi - start, RICHTEXT_URL);
                p += n;
                continue;
            }
        }
        emit(out, out_size, oi, p, 1);
        p++;
    }
}

size_t richtext_parse(const char *text, char *out, size_t out_size,
                      size_t *out_len, richtext_run_t *runs, size_t max_runs) {
    size_t oi = 0;
    size_t rc = 0;
    const char *p = text;

    if (out && out_size > 0) {
        out[0] = '\0';
    }
    if (out_len) {
        *out_len = 0;
    }
    if (!text || !out || out_size == 0) {
        return 0;
    }

    while (*p) {
        size_t len = line_length(p);

        if (is_fence(p, len)) {
            const char *body = p[len] ? p + len + 1 : p + len;
            const char *close = find_closing_fence(body);

            if (close) {
                size_t start = oi;
                size_t body_len = (size_t)(close - body);

                /* Drop the newline that ended the last line of the block. */
                if (body_len > 0 && body[body_len - 1] == '\n') {
                    body_len--;
                }
                emit(out, out_size, &oi, body, body_len);
                add_run(runs, max_runs, &rc, start, oi - start, RICHTEXT_CODE);

                p = close + line_length(close);
                if (*p == '\n') {
                    p++;
                    if (*p) {
                        emit(out, out_size, &oi, "\n", 1);
                    }
                }
                continue;
            }
        }

        parse_inline(p, p + len, out, out_size, &oi, runs, max_runs, &rc);
        p += len;
        if (*p == '\n') {
            emit(out, out_size, &oi, "\n", 1);
            p++;
        }
    }

    out[oi] = '\0';
    if (out_len) {
        *out_len = oi;
    }
    return rc;
}

const char *richtext_style_on(richtext_style_t style) {
    switch (style) {
        case RICHTEXT_BOLD: return "\033[1m";
        case RICHTEXT_CODE: return "\033[36m";
        case RICHTEXT_URL:  return "\033[4m";
        default:            return "";
    }
}

const char *richtext_style_off(richtext_style_t style) {
    switch (style) {
        case RICHTEXT_BOLD: return "\033[22m";
        case RICHTEXT_CODE: return "\033[39m";
        case RICHTEXT_URL:  return "\033[24m";
        default:            return "";
    }
}
