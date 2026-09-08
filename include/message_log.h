#ifndef MESSAGE_LOG_H
#define MESSAGE_LOG_H

#include "message.h"

#define MESSAGE_LOG_MAX_LINE 2048

void message_log_format_timestamp_utc(time_t ts, char *buffer,
                                      size_t buf_size);

/* First line of a migrated log.  Both the old and the new parser treat it as
 * an unparseable record and skip it, which is what makes it safe to add to a
 * file an older binary may still read. */
#define MESSAGE_LOG_HEADER "#tnt-message-log v2"

bool message_log_is_header(const char *line);

/* Content is stored escaped so a message may contain a newline while records
 * stay one per line: `\` becomes `\\`, a newline becomes `\n`. */
bool message_log_encode_content(const char *in, char *out, size_t out_size);
bool message_log_decode_content(const char *in, char *out, size_t out_size);

/* Bytes the content occupies once escaped, excluding the terminator.  The
 * 1023-byte field limit applies to this number, not to the decoded text, so
 * the input gauge and the send guard both count it. */
size_t message_log_encoded_length(const char *in);

/* Rewrite one pre-v2 log line with its content field escaped, preserving the
 * line's own terminator.  A line that is not a record is copied through: the
 * parser already skips it.  False means the record cannot be represented in
 * v2, so it is dropped rather than rewritten to decode as something else. */
bool message_log_upgrade_record(const char *line, char *out, size_t out_size);

/* Bring a pre-v2 log up to the escaped format in place: back it up to
 * `<path>.v1.bak`, escape every content field, and prepend the header.  A
 * missing file and a file that already carries the header are both no-ops,
 * which is what makes this safe to run at every start.  Returns -1 only when
 * the log was left untouched by a failure. */
int message_log_migrate(const char *path);

/* Parse one complete messages.log record and decode its content.  `now` is
 * used to reject records outside TNT's accepted replay window. */
bool message_log_parse_record(const char *line, message_t *out, time_t now);

/* Format one messages.log record, escaping its content.  record_len receives the number of bytes
 * that would be written, excluding the trailing NUL.  Passing NULL/0 for the
 * output buffer is allowed when only the length is needed. */
int message_log_format_record(const message_t *msg, char *buffer,
                              size_t buf_size, size_t *record_len);

#endif /* MESSAGE_LOG_H */
