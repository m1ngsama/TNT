#ifndef JSON_TEXT_H
#define JSON_TEXT_H

#include "common.h"

void tnt_json_append_string(char *buffer, size_t buf_size, size_t *pos,
                            const char *text);

/* Extract a top-level JSON string field from a single JSON object.
 * Returns false for malformed JSON, missing key, non-string value, or output
 * overflow. Unknown nested objects and arrays are skipped. */
bool tnt_json_get_string_field(const char *json, const char *key,
                               char *out, size_t out_size);

/* Extract a top-level JSON array of strings into `max_items` slots of
 * `item_size` bytes each and store the element count in `count`.  Returns
 * false for malformed JSON, a missing key, a non-string element, an element
 * overflow, or more than `max_items` elements. */
bool tnt_json_get_string_array_field(const char *json, const char *key,
                                     char *items, size_t item_size,
                                     size_t max_items, size_t *count);

#endif /* JSON_TEXT_H */
