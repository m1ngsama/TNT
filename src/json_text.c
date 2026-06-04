#include "json_text.h"

#include <ctype.h>

void tnt_json_append_string(char *buffer, size_t buf_size, size_t *pos,
                            const char *text) {
    const unsigned char *p = (const unsigned char *)(text ? text : "");

    buffer_append_bytes(buffer, buf_size, pos, "\"", 1);

    while (*p && pos && *pos < buf_size - 1) {
        char escaped[7];

        switch (*p) {
            case '\\':
                buffer_append_bytes(buffer, buf_size, pos, "\\\\", 2);
                break;
            case '"':
                buffer_append_bytes(buffer, buf_size, pos, "\\\"", 2);
                break;
            case '\b':
                buffer_append_bytes(buffer, buf_size, pos, "\\b", 2);
                break;
            case '\f':
                buffer_append_bytes(buffer, buf_size, pos, "\\f", 2);
                break;
            case '\n':
                buffer_append_bytes(buffer, buf_size, pos, "\\n", 2);
                break;
            case '\r':
                buffer_append_bytes(buffer, buf_size, pos, "\\r", 2);
                break;
            case '\t':
                buffer_append_bytes(buffer, buf_size, pos, "\\t", 2);
                break;
            default:
                if (*p < 0x20) {
                    snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
                    buffer_append_bytes(buffer, buf_size, pos,
                                        escaped, strlen(escaped));
                } else {
                    buffer_append_bytes(buffer, buf_size, pos,
                                        (const char *)p, 1);
                }
                break;
        }
        p++;
    }

    buffer_append_bytes(buffer, buf_size, pos, "\"", 1);
}

static const char *skip_ws(const char *p) {
    while (p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_hex4(const char *p, uint32_t *out) {
    uint32_t cp = 0;

    if (!p || !out) return false;

    for (int i = 0; i < 4; i++) {
        int v = hex_value(p[i]);
        if (v < 0) return false;
        cp = (cp << 4) | (uint32_t)v;
    }

    *out = cp;
    return true;
}

static bool append_decoded_codepoint(char *out, size_t out_size,
                                     size_t *pos, uint32_t cp) {
    char bytes[4];
    size_t len;

    if (!out || !pos || out_size == 0 || cp == 0 ||
        cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return false;
    }

    if (cp <= 0x7F) {
        bytes[0] = (char)cp;
        len = 1;
    } else if (cp <= 0x7FF) {
        bytes[0] = (char)(0xC0 | (cp >> 6));
        bytes[1] = (char)(0x80 | (cp & 0x3F));
        len = 2;
    } else if (cp <= 0xFFFF) {
        bytes[0] = (char)(0xE0 | (cp >> 12));
        bytes[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        bytes[2] = (char)(0x80 | (cp & 0x3F));
        len = 3;
    } else {
        bytes[0] = (char)(0xF0 | (cp >> 18));
        bytes[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        bytes[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        bytes[3] = (char)(0x80 | (cp & 0x3F));
        len = 4;
    }

    if (*pos + len >= out_size) {
        return false;
    }

    memcpy(out + *pos, bytes, len);
    *pos += len;
    out[*pos] = '\0';
    return true;
}

static bool append_byte(char *out, size_t out_size, size_t *pos, char c) {
    if (!out || !pos || out_size == 0 || *pos + 1 >= out_size) {
        return false;
    }

    out[(*pos)++] = c;
    out[*pos] = '\0';
    return true;
}

static bool parse_json_string(const char **cursor, char *out,
                              size_t out_size) {
    const char *p;
    size_t pos = 0;

    if (!cursor || !*cursor || **cursor != '"' || !out || out_size == 0) {
        return false;
    }

    out[0] = '\0';
    p = *cursor + 1;

    while (*p) {
        unsigned char c = (unsigned char)*p++;

        if (c == '"') {
            *cursor = p;
            return true;
        }

        if (c < 0x20) {
            return false;
        }

        if (c != '\\') {
            if (!append_byte(out, out_size, &pos, (char)c)) {
                return false;
            }
            continue;
        }

        char esc = *p++;
        switch (esc) {
            case '"':
            case '\\':
            case '/':
                if (!append_byte(out, out_size, &pos, esc)) return false;
                break;
            case 'b':
                if (!append_byte(out, out_size, &pos, '\b')) return false;
                break;
            case 'f':
                if (!append_byte(out, out_size, &pos, '\f')) return false;
                break;
            case 'n':
                if (!append_byte(out, out_size, &pos, '\n')) return false;
                break;
            case 'r':
                if (!append_byte(out, out_size, &pos, '\r')) return false;
                break;
            case 't':
                if (!append_byte(out, out_size, &pos, '\t')) return false;
                break;
            case 'u': {
                uint32_t cp;
                if (!parse_hex4(p, &cp)) return false;
                p += 4;

                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    uint32_t low;
                    if (p[0] != '\\' || p[1] != 'u' ||
                        !parse_hex4(p + 2, &low) ||
                        low < 0xDC00 || low > 0xDFFF) {
                        return false;
                    }
                    p += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return false;
                }

                if (!append_decoded_codepoint(out, out_size, &pos, cp)) {
                    return false;
                }
                break;
            }
            default:
                return false;
        }
    }

    return false;
}

static bool skip_json_string(const char **cursor) {
    const char *p;

    if (!cursor || !*cursor || **cursor != '"') {
        return false;
    }

    p = *cursor + 1;
    while (*p) {
        unsigned char c = (unsigned char)*p++;
        if (c == '"') {
            *cursor = p;
            return true;
        }
        if (c < 0x20) return false;
        if (c == '\\') {
            if (*p == 'u') {
                uint32_t ignored;
                if (!parse_hex4(p + 1, &ignored)) return false;
                p += 5;
            } else if (*p) {
                p++;
            } else {
                return false;
            }
        }
    }

    return false;
}

static bool skip_json_value(const char **cursor) {
    const char *p;

    if (!cursor || !*cursor) return false;
    p = skip_ws(*cursor);

    if (*p == '"') {
        if (!skip_json_string(&p)) return false;
        *cursor = p;
        return true;
    }

    if (*p == '{' || *p == '[') {
        int depth = 0;
        do {
            if (*p == '"' && !skip_json_string(&p)) {
                return false;
            }
            if (*p == '{' || *p == '[') {
                depth++;
                p++;
            } else if (*p == '}' || *p == ']') {
                depth--;
                p++;
            } else if (*p) {
                p++;
            } else {
                return false;
            }
        } while (depth > 0);

        *cursor = p;
        return true;
    }

    while (*p && *p != ',' && *p != '}' && *p != ']') {
        p++;
    }

    *cursor = p;
    return true;
}

bool tnt_json_get_string_field(const char *json, const char *key,
                               char *out, size_t out_size) {
    const char *p;
    bool found = false;

    if (!json || !key || key[0] == '\0' || !out || out_size == 0) {
        return false;
    }

    out[0] = '\0';
    p = skip_ws(json);
    if (*p != '{') {
        return false;
    }
    p++;

    while (1) {
        char parsed_key[128];

        p = skip_ws(p);
        if (*p == '}') {
            return false;
        }
        if (*p != '"' || !parse_json_string(&p, parsed_key,
                                            sizeof(parsed_key))) {
            return false;
        }

        p = skip_ws(p);
        if (*p != ':') {
            return false;
        }
        p = skip_ws(p + 1);

        if (strcmp(parsed_key, key) == 0) {
            if (*p != '"' || !parse_json_string(&p, out, out_size)) {
                return false;
            }
            found = true;
        } else if (!skip_json_value(&p)) {
            return false;
        }

        p = skip_ws(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            return found;
        }
        return false;
    }
}
