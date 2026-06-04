#include "input_buffer.h"

#include "utf8.h"

void tnt_input_utf8_state_reset(tnt_input_utf8_state_t *state) {
    if (!state) return;
    state->len = 0;
    state->expected_len = 0;
    memset(state->bytes, 0, sizeof(state->bytes));
}

static int append_bytes(char *input, size_t input_size, const char *bytes,
                        size_t len) {
    size_t cur;

    if (!input || !bytes || input_size == 0 || len == 0) {
        return TNT_INPUT_APPEND_IGNORED;
    }

    cur = strlen(input);
    if (cur + len >= input_size) {
        return TNT_INPUT_APPEND_OVERFLOW;
    }

    memcpy(input + cur, bytes, len);
    input[cur + len] = '\0';
    return TNT_INPUT_APPEND_OK;
}

int tnt_input_append_ascii(char *input, size_t input_size, unsigned char b) {
    char c = (char)b;

    if (b < 32 || b >= 127) {
        return TNT_INPUT_APPEND_IGNORED;
    }

    return append_bytes(input, input_size, &c, 1);
}

int tnt_input_append_utf8_sequence(char *input, size_t input_size,
                                   const char *bytes, int len) {
    if (!bytes || len <= 0 || len > 4 ||
        !utf8_is_valid_sequence(bytes, len)) {
        return TNT_INPUT_APPEND_INVALID_UTF8;
    }

    return append_bytes(input, input_size, bytes, (size_t)len);
}

static int append_printable_byte(char *input, size_t input_size,
                                 tnt_input_utf8_state_t *state,
                                 unsigned char b, bool paste_mode);

static int start_utf8_sequence(char *input, size_t input_size,
                               tnt_input_utf8_state_t *state,
                               unsigned char b, bool paste_mode) {
    int expected = utf8_byte_length(b);

    if (expected <= 1 || expected > 4) {
        tnt_input_utf8_state_reset(state);
        return TNT_INPUT_APPEND_INVALID_UTF8;
    }

    state->bytes[0] = (char)b;
    state->len = 1;
    state->expected_len = expected;

    if (expected == 1) {
        int status = tnt_input_append_utf8_sequence(input, input_size,
                                                    state->bytes, 1);
        tnt_input_utf8_state_reset(state);
        return status;
    }

    (void)paste_mode;
    return TNT_INPUT_APPEND_OK;
}

static int append_printable_byte(char *input, size_t input_size,
                                 tnt_input_utf8_state_t *state,
                                 unsigned char b, bool paste_mode) {
    int status = TNT_INPUT_APPEND_OK;

    if (b < 128) {
        if (state && state->len > 0) {
            tnt_input_utf8_state_reset(state);
            status |= TNT_INPUT_APPEND_INVALID_UTF8;
        }
        status |= tnt_input_append_ascii(input, input_size, b);
        return status;
    }

    if (!state) {
        return TNT_INPUT_APPEND_INVALID_UTF8;
    }

    if (state->len == 0) {
        return start_utf8_sequence(input, input_size, state, b, paste_mode);
    }

    if ((b & 0xC0) != 0x80) {
        tnt_input_utf8_state_reset(state);
        status |= TNT_INPUT_APPEND_INVALID_UTF8;
        status |= append_printable_byte(input, input_size, state, b,
                                        paste_mode);
        return status;
    }

    state->bytes[state->len++] = (char)b;
    if (state->len == state->expected_len) {
        status |= tnt_input_append_utf8_sequence(input, input_size,
                                                 state->bytes, state->len);
        tnt_input_utf8_state_reset(state);
    }

    return status;
}

int tnt_input_append_stream_byte(char *input, size_t input_size,
                                 tnt_input_utf8_state_t *state,
                                 unsigned char b, bool paste_mode) {
    int status = TNT_INPUT_APPEND_OK;

    if (paste_mode && (b == '\r' || b == '\n' || b == '\t')) {
        b = ' ';
    }

    if (b < 32) {
        if (state && state->len > 0) {
            tnt_input_utf8_state_reset(state);
            status |= TNT_INPUT_APPEND_INVALID_UTF8;
        }
        return status | TNT_INPUT_APPEND_IGNORED;
    }

    return status | append_printable_byte(input, input_size, state, b,
                                          paste_mode);
}

int tnt_input_utf8_state_finish(tnt_input_utf8_state_t *state) {
    if (!state || state->len == 0) {
        return TNT_INPUT_APPEND_OK;
    }

    tnt_input_utf8_state_reset(state);
    return TNT_INPUT_APPEND_INVALID_UTF8;
}
