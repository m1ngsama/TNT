#ifndef UTF8_H
#define UTF8_H

#include "common.h"

/* UTF-8 character width calculation */
int utf8_char_width(uint32_t codepoint);

/* Get the number of bytes in a UTF-8 character from its first byte */
int utf8_byte_length(unsigned char first_byte);

/* Decode a UTF-8 character and return its codepoint */
uint32_t utf8_decode(const char *str, int *bytes_read);

/* Calculate display width of a UTF-8 string (considering CJK double-width) */
int utf8_string_width(const char *str);

/* Truncate string to fit within max_width display characters */
void utf8_truncate(char *str, int max_width);

/* Count the number of UTF-8 characters in a string */
int utf8_strlen(const char *str);

/* Remove last UTF-8 character from string */
void utf8_remove_last_char(char *str);

#endif /* UTF8_H */
