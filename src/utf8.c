#include "utf8.h"
#include "common.h"

/* Get the number of bytes in a UTF-8 character from its first byte */
int utf8_byte_length(unsigned char first_byte) {
    if ((first_byte & 0x80) == 0) return 1;      /* 0xxxxxxx */
    if ((first_byte & 0xE0) == 0xC0) return 2;   /* 110xxxxx */
    if ((first_byte & 0xF0) == 0xE0) return 3;   /* 1110xxxx */
    if ((first_byte & 0xF8) == 0xF0) return 4;   /* 11110xxx */
    return 1; /* Invalid UTF-8, treat as single byte */
}

/* Decode a UTF-8 character and return its codepoint */
uint32_t utf8_decode(const char *str, int *bytes_read) {
    const unsigned char *s = (const unsigned char *)str;
    uint32_t codepoint = 0;
    int len = utf8_byte_length(s[0]);

    if (len < 1 || len > 4) {
        len = 1;
    }

    for (int i = 1; i < len; i++) {
        if (s[i] == '\0' || (s[i] & 0xC0) != 0x80) {
            /* Truncated or invalid continuation byte — treat as single byte */
            *bytes_read = 1;
            return s[0];
        }
    }

    *bytes_read = len;

    switch (len) {
        case 1:
            codepoint = s[0];
            break;
        case 2:
            codepoint = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
            break;
        case 3:
            codepoint = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
            break;
        case 4:
            codepoint = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
                       ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
            break;
    }

    return codepoint;
}

/* UTF-8 character width calculation for CJK and other wide characters */
int utf8_char_width(uint32_t codepoint) {
    /* ASCII */
    if (codepoint < 0x80) return 1;

    /* CJK Unified Ideographs */
    if ((codepoint >= 0x4E00 && codepoint <= 0x9FFF) ||    /* CJK Unified */
        (codepoint >= 0x3400 && codepoint <= 0x4DBF) ||    /* CJK Extension A */
        (codepoint >= 0x20000 && codepoint <= 0x2A6DF) ||  /* CJK Extension B */
        (codepoint >= 0x2A700 && codepoint <= 0x2B73F) ||  /* CJK Extension C */
        (codepoint >= 0x2B740 && codepoint <= 0x2B81F) ||  /* CJK Extension D */
        (codepoint >= 0x2B820 && codepoint <= 0x2CEAF) ||  /* CJK Extension E */
        (codepoint >= 0xF900 && codepoint <= 0xFAFF) ||    /* CJK Compatibility */
        (codepoint >= 0x2F800 && codepoint <= 0x2FA1F)) {  /* CJK Compat Suppl */
        return 2;
    }

    /* Hangul Syllables (Korean) */
    if (codepoint >= 0xAC00 && codepoint <= 0xD7AF) return 2;

    /* Hiragana and Katakana (Japanese) */
    if ((codepoint >= 0x3040 && codepoint <= 0x309F) ||    /* Hiragana */
        (codepoint >= 0x30A0 && codepoint <= 0x30FF)) {    /* Katakana */
        return 2;
    }

    /* Fullwidth forms */
    if (codepoint >= 0xFF00 && codepoint <= 0xFFEF) return 2;

    /* Zero-width: combining marks, variation selectors, joiners, and the
     * zero-width space family.  These attach to a base character and add no
     * columns of their own. */
    if ((codepoint >= 0x0300 && codepoint <= 0x036F) ||   /* Combining Diacritical */
        (codepoint >= 0x1AB0 && codepoint <= 0x1AFF) ||   /* Combining Extended */
        (codepoint >= 0x1DC0 && codepoint <= 0x1DFF) ||   /* Combining Supplement */
        (codepoint >= 0x20D0 && codepoint <= 0x20FF) ||   /* Combining Symbols */
        (codepoint >= 0xFE00 && codepoint <= 0xFE0F) ||   /* Variation Selectors */
        (codepoint >= 0xFE20 && codepoint <= 0xFE2F) ||   /* Combining Half Marks */
        (codepoint >= 0x1160 && codepoint <= 0x11FF) ||   /* Hangul Jamo medial/final */
        codepoint == 0x200B || codepoint == 0x200C ||
        codepoint == 0x200D ||                            /* ZWSP, ZWNJ, ZWJ */
        (codepoint >= 0x2060 && codepoint <= 0x2064)) {
        return 0;
    }

    /* Emoji and other wide pictographs. */
    if ((codepoint >= 0x1F300 && codepoint <= 0x1F5FF) ||  /* Misc Pictographs */
        (codepoint >= 0x1F600 && codepoint <= 0x1F64F) ||  /* Emoticons */
        (codepoint >= 0x1F680 && codepoint <= 0x1F6FF) ||  /* Transport */
        (codepoint >= 0x1F900 && codepoint <= 0x1F9FF) ||  /* Supplemental */
        (codepoint >= 0x1FA70 && codepoint <= 0x1FAFF) ||  /* Extended-A */
        (codepoint >= 0x1F000 && codepoint <= 0x1F0FF) ||  /* Tiles and cards */
        (codepoint >= 0x1F1E6 && codepoint <= 0x1F1FF)) {  /* Regional indicators */
        return 2;
    }

    /* Default to single width */
    return 1;
}

static bool codepoint_is_regional_indicator(uint32_t cp) {
    return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

static bool codepoint_joins_previous(uint32_t cp) {
    /* Anything that renders as part of the preceding cluster. */
    return utf8_char_width(cp) == 0;
}

size_t utf8_cluster_length(const char *str) {
    int used = 0;
    uint32_t base;
    size_t len;

    if (!str || *str == '\0') {
        return 0;
    }

    base = utf8_decode(str, &used);
    if (used <= 0) {
        return 1;
    }
    len = (size_t)used;

    for (;;) {
        const char *next = str + len;
        int next_used = 0;
        uint32_t cp;

        if (*next == '\0') {
            break;
        }

        cp = utf8_decode(next, &next_used);
        if (next_used <= 0) {
            break;
        }

        if (cp == 0x200D) {
            /* A joiner always pulls in whatever follows it. */
            int joined_used = 0;
            const char *joined = next + next_used;

            if (*joined == '\0') {
                break;
            }
            utf8_decode(joined, &joined_used);
            if (joined_used <= 0) {
                break;
            }
            len += (size_t)next_used + (size_t)joined_used;
            continue;
        }

        if (codepoint_joins_previous(cp)) {
            len += (size_t)next_used;
            continue;
        }

        if (codepoint_is_regional_indicator(base) &&
            codepoint_is_regional_indicator(cp)) {
            /* A flag is exactly two indicators; a third starts a new flag. */
            len += (size_t)next_used;
        }
        break;
    }

    return len;
}

int utf8_cluster_width(const char *str) {
    size_t len = utf8_cluster_length(str);
    size_t offset = 0;
    int width = 0;
    bool emoji_presentation = false;

    if (len == 0) {
        return 0;
    }

    while (offset < len) {
        int used = 0;
        uint32_t cp = utf8_decode(str + offset, &used);

        if (used <= 0) {
            break;
        }
        if (cp == 0xFE0F) {
            emoji_presentation = true;
        }
        if (width == 0) {
            width = utf8_char_width(cp);
        }
        offset += (size_t)used;
    }

    if (emoji_presentation && width < 2) {
        width = 2;
    }
    return width == 0 ? 0 : width;
}

/* Calculate display width of a UTF-8 string */
int utf8_string_width(const char *str) {
    int width = 0;

    if (!str) {
        return 0;
    }
    while (*str) {
        size_t len = utf8_cluster_length(str);

        if (len == 0) {
            break;
        }
        width += utf8_cluster_width(str);
        str += len;
    }
    return width;
}

static const char *skip_ansi_sequence(const char *p) {
    if (!p || *p != '\033') {
        return p;
    }

    if (p[1] == '[') {
        const char *q = p + 2;
        while (*q) {
            unsigned char c = (unsigned char)*q;
            if (c >= 0x40 && c <= 0x7E) {
                return q + 1;
            }
            q++;
        }
        return p + 1;
    }

    return p[1] ? p + 2 : p + 1;
}

int utf8_ansi_string_width(const char *str) {
    int width = 0;
    int bytes_read;
    const char *p = str;

    if (!str) {
        return 0;
    }

    while (*p != '\0') {
        if (*p == '\033') {
            p = skip_ansi_sequence(p);
            continue;
        }

        uint32_t codepoint = utf8_decode(p, &bytes_read);
        width += utf8_char_width(codepoint);
        p += bytes_read;
    }

    return width;
}

/* Count the number of UTF-8 characters in a string */
int utf8_strlen(const char *str) {
    int count = 0;
    int bytes_read;
    const char *p = str;

    while (*p != '\0') {
        utf8_decode(p, &bytes_read);
        count++;
        p += bytes_read;
    }

    return count;
}

/* Truncate string to fit within max_width display characters */
void utf8_truncate(char *str, int max_width) {
    size_t offset = 0;
    int width = 0;

    if (!str || max_width < 0) {
        return;
    }

    while (str[offset]) {
        size_t len = utf8_cluster_length(str + offset);
        int cluster_width = utf8_cluster_width(str + offset);

        if (len == 0 || width + cluster_width > max_width) {
            break;
        }
        width += cluster_width;
        offset += len;
    }
    str[offset] = '\0';
}

void utf8_ansi_truncate(const char *src, char *dst, size_t dst_size,
                        int max_width) {
    int width = 0;
    size_t pos = 0;
    bool copied_ansi = false;
    bool last_ansi_was_reset = false;
    bool truncated = false;
    const char *p = src;

    if (!dst || dst_size == 0) {
        return;
    }

    dst[0] = '\0';
    if (!src || max_width <= 0) {
        return;
    }

    while (*p != '\0') {
        if (*p == '\033') {
            const char *end = skip_ansi_sequence(p);
            size_t len = (size_t)(end - p);

            if (pos + len >= dst_size) {
                truncated = true;
                break;
            }
            memcpy(dst + pos, p, len);
            pos += len;
            copied_ansi = true;
            last_ansi_was_reset =
                len == strlen(ANSI_RESET) && memcmp(p, ANSI_RESET, len) == 0;
            p = end;
            continue;
        }

        size_t cluster_len = utf8_cluster_length(p);
        int cluster_width = utf8_cluster_width(p);

        if (cluster_len == 0) {
            break;
        }
        if (width + cluster_width > max_width) {
            truncated = true;
            break;
        }
        if (pos + cluster_len >= dst_size) {
            truncated = true;
            break;
        }

        memcpy(dst + pos, p, cluster_len);
        pos += cluster_len;
        width += cluster_width;
        p += cluster_len;
    }

    if (truncated && copied_ansi && !last_ansi_was_reset) {
        size_t reset_len = strlen(ANSI_RESET);
        if (pos + reset_len < dst_size) {
            memcpy(dst + pos, ANSI_RESET, reset_len);
            pos += reset_len;
        }
    }

    dst[pos] = '\0';
}

/* Remove last UTF-8 character from string */
size_t utf8_remove_last_char(char *str, size_t len) {
    if (len == 0) return 0;

    /* Find the start of the last character by walking backwards */
    size_t i = len - 1;
    while (i > 0 && (str[i] & 0xC0) == 0x80) {
        i--;  /* Continue byte of multi-byte sequence */
    }

    str[i] = '\0';
    return i;
}

/* Remove last word from string (mimic Ctrl+W) */
size_t utf8_remove_last_word(char *str, size_t len) {
    size_t i = len;

    /* Skip trailing spaces */
    while (i > 0 && str[i - 1] == ' ') {
        i--;
    }

    /* Skip non-spaces (the word) */
    while (i > 0 && str[i - 1] != ' ') {
        i--;
    }

    str[i] = '\0';
    return i;
}

/* Validate a UTF-8 byte sequence */
bool utf8_is_valid_sequence(const char *bytes, int len) {
    if (len <= 0 || len > 4 || !bytes) {
        return false;
    }

    const unsigned char *b = (const unsigned char *)bytes;

    /* Check first byte matches the expected length */
    int expected_len = utf8_byte_length(b[0]);
    if (expected_len != len) {
        return false;
    }

    /* Validate continuation bytes (must be 10xxxxxx) */
    for (int i = 1; i < len; i++) {
        if ((b[i] & 0xC0) != 0x80) {
            return false;
        }
    }

    /* Validate codepoint ranges to prevent overlong encodings */
    uint32_t codepoint = 0;
    switch (len) {
        case 1:
            /* 0xxxxxxx - valid range: 0x01-0x7F (reject NUL) */
            codepoint = b[0];
            if (codepoint == 0 || codepoint > 0x7F) return false;
            break;
        case 2:
            /* 110xxxxx 10xxxxxx - valid range: 0x80-0x7FF */
            codepoint = ((b[0] & 0x1F) << 6) | (b[1] & 0x3F);
            if (codepoint < 0x80 || codepoint > 0x7FF) return false;
            break;
        case 3:
            /* 1110xxxx 10xxxxxx 10xxxxxx - valid range: 0x800-0xFFFF */
            codepoint = ((b[0] & 0x0F) << 12) | ((b[1] & 0x3F) << 6) | (b[2] & 0x3F);
            if (codepoint < 0x800 || codepoint > 0xFFFF) return false;
            /* Reject UTF-16 surrogates (0xD800-0xDFFF) */
            if (codepoint >= 0xD800 && codepoint <= 0xDFFF) return false;
            break;
        case 4:
            /* 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx - valid range: 0x10000-0x10FFFF */
            codepoint = ((b[0] & 0x07) << 18) | ((b[1] & 0x3F) << 12) |
                       ((b[2] & 0x3F) << 6) | (b[3] & 0x3F);
            if (codepoint < 0x10000 || codepoint > 0x10FFFF) return false;
            break;
    }

    return true;
}

bool utf8_is_valid_string(const char *str) {
    const unsigned char *p = (const unsigned char *)str;

    if (!str) {
        return false;
    }

    while (*p != '\0') {
        int len = utf8_byte_length(*p);
        if (len < 1 || len > 4) {
            return false;
        }

        for (int i = 1; i < len; i++) {
            if (p[i] == '\0') {
                return false;
            }
        }

        if (!utf8_is_valid_sequence((const char *)p, len)) {
            return false;
        }

        p += len;
    }

    return true;
}

bool utf8_is_control_sequence(const char *bytes, int len) {
    const unsigned char *p = (const unsigned char *)bytes;

    if (!bytes || len <= 0) {
        return false;
    }
    return (len == 1 && (p[0] < 0x20 || p[0] == 0x7F)) ||
           (len == 2 && p[0] == 0xC2 && p[1] >= 0x80 && p[1] <= 0x9F);
}

bool utf8_contains_control(const char *str) {
    if (!str) {
        return false;
    }

    for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
        if (*p < 0x20 || *p == 0x7F) {
            return true;
        }
        if (*p == 0xC2 && p[1] >= 0x80 && p[1] <= 0x9F) {
            return true;
        }
    }

    return false;
}
