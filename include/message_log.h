#ifndef MESSAGE_LOG_H
#define MESSAGE_LOG_H

#include "message.h"

#define MESSAGE_LOG_MAX_LINE 2048

void message_log_format_timestamp_utc(time_t ts, char *buffer,
                                      size_t buf_size);

/* Parse one complete messages.log v1 record.  `now` is used to reject records
 * outside TNT's accepted replay window. */
bool message_log_parse_record(const char *line, message_t *out, time_t now);

/* Format one messages.log v1 record.  record_len receives the number of bytes
 * that would be written, excluding the trailing NUL.  Passing NULL/0 for the
 * output buffer is allowed when only the length is needed. */
int message_log_format_record(const message_t *msg, char *buffer,
                              size_t buf_size, size_t *record_len);

#endif /* MESSAGE_LOG_H */
