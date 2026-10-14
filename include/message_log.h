#ifndef MESSAGE_LOG_H
#define MESSAGE_LOG_H

#include "message.h"

#define MESSAGE_LOG_MAX_LINE 2048

void message_log_format_timestamp_utc(time_t ts, char *buffer,
                                      size_t buf_size);

/* First line of a log in the current format.  Every parser treats a marker
 * line as an unparseable record and skips it. */
#define MESSAGE_LOG_VERSION 3
#define MESSAGE_LOG_HEADER "#tnt-message-log v3"
#define MESSAGE_LOG_FLAG_VERIFIED 'v'

/* The version a marker line names (2 or 3), or 0 when the line is not a
 * marker.  A file without a marker is version 1. */
int message_log_header_version(const char *line);

/* Content is stored escaped so a message may contain a newline while records
 * stay one per line: `\` becomes `\\`, a newline becomes `\n`. */
bool message_log_encode_content(const char *in, char *out, size_t out_size);
bool message_log_decode_content(const char *in, char *out, size_t out_size);

/* Escaped length: the field limit applies to this, not to the decoded text. */
size_t message_log_encoded_length(const char *in);

/* The one definition of which control characters message content may carry:
 * a newline, and nothing else below 0x20, no DEL, no C1. */
bool message_log_content_has_forbidden_control(const char *s);

/* Rewrite one line of a version `from_version` log as a current record,
 * preserving the line's own terminator: a v1 content field is escaped, and
 * every record gains an empty flags field.  A line that is not a record is
 * copied through: the parser already skips it.  False means the record
 * cannot be represented, so it is dropped rather than rewritten to decode as
 * something else. */
bool message_log_upgrade_record(const char *line, int from_version, char *out,
                                size_t out_size);

/* Bring an older log up to the current format in place: back it up to
 * `<path>.v1.bak` or `<path>.v2.bak`, upgrade every record, and prepend the
 * current header.  A missing file and a file that already carries the
 * current header are both no-ops, which is what makes this safe to run at
 * every start.  Returns -1 only when the log was left untouched by a
 * failure. */
int message_log_migrate(const char *path);

/* Parse one complete current record and decode its content.  `now` is used
 * to reject records outside TNT's accepted replay window. */
bool message_log_parse_record(const char *line, message_t *out, time_t now);

/* Format one record, escaping its content.  Without `with_flags` the record
 * stops after the content field, the form exec dump has always written.
 * record_len receives the number of bytes that would be written, excluding
 * the trailing NUL.  Passing NULL/0 for the output buffer is allowed when
 * only the length is needed. */
int message_log_format_record(const message_t *msg, bool with_flags,
                              char *buffer, size_t buf_size,
                              size_t *record_len);

#endif /* MESSAGE_LOG_H */
