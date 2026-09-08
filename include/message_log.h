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

/* Parse one complete messages.log record and decode its content.  `now` is
 * used to reject records outside TNT's accepted replay window. */
bool message_log_parse_record(const char *line, message_t *out, time_t now);

/* Format one messages.log record, escaping its content.  record_len receives the number of bytes
 * that would be written, excluding the trailing NUL.  Passing NULL/0 for the
 * output buffer is allowed when only the length is needed. */
int message_log_format_record(const message_t *msg, char *buffer,
                              size_t buf_size, size_t *record_len);

#endif /* MESSAGE_LOG_H */
