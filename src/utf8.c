#include "utf8.h"

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

    /* Default to single width */
    return 1;
}

/* Calculate display width of a UTF-8 string */
int utf8_string_width(const char *str) {
    int width = 0;
    int bytes_read;
    const char *p = str;

    while (*p != '\0') {
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
    int width = 0;
    int bytes_read;
    char *p = str;
    char *last_valid = str;

    while (*p != '\0') {
        uint32_t codepoint = utf8_decode(p, &bytes_read);
        int char_width = utf8_char_width(codepoint);

        if (width + char_width > max_width) {
            break;
        }

        width += char_width;
        p += bytes_read;
        last_valid = p;
    }

    *last_valid = '\0';
}

/* Remove last UTF-8 character from string */
void utf8_remove_last_char(char *str) {
    int len = strlen(str);
    if (len == 0) return;

    /* Find the start of the last character by walking backwards */
    int i = len - 1;
    while (i > 0 && (str[i] & 0xC0) == 0x80) {
        i--;  /* Continue byte of multi-byte sequence */
    }

    str[i] = '\0';
}
