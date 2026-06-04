#ifndef INPUT_BUFFER_H
#define INPUT_BUFFER_H

#include "common.h"

typedef enum {
    TNT_INPUT_APPEND_OK = 0,
    TNT_INPUT_APPEND_IGNORED = 1 << 0,
    TNT_INPUT_APPEND_OVERFLOW = 1 << 1,
    TNT_INPUT_APPEND_INVALID_UTF8 = 1 << 2
} tnt_input_append_status_t;

typedef struct {
    char bytes[4];
    int len;
    int expected_len;
} tnt_input_utf8_state_t;

void tnt_input_utf8_state_reset(tnt_input_utf8_state_t *state);

int tnt_input_append_ascii(char *input, size_t input_size, unsigned char b);
int tnt_input_append_utf8_sequence(char *input, size_t input_size,
                                   const char *bytes, int len);

/* Append one byte from a terminal stream, validating UTF-8 across calls.
 * In paste mode CR/LF/TAB are normalized to spaces so existing TNT 1.x
 * single-line message semantics are preserved. */
int tnt_input_append_stream_byte(char *input, size_t input_size,
                                 tnt_input_utf8_state_t *state,
                                 unsigned char b, bool paste_mode);

/* Returns TNT_INPUT_APPEND_INVALID_UTF8 when the stream ended mid-codepoint. */
int tnt_input_utf8_state_finish(tnt_input_utf8_state_t *state);

#endif /* INPUT_BUFFER_H */
