#include "message_log_tool.h"

#include "message_log.h"

#include <errno.h>

typedef struct {
    long records_seen;
    long valid_records;
    long invalid_records;
    long first_invalid_line;
} message_log_report_t;

static void discard_line_remainder(FILE *fp) {
    int c;

    while ((c = fgetc(fp)) != '\n' && c != EOF) {
    }
}

static int print_recovered_record(const message_t *msg) {
    char record[MAX_USERNAME_LEN + MAX_MESSAGE_LEN + 48];
    size_t record_len = 0;

    if (message_log_format_record(msg, record, sizeof(record),
                                  &record_len) < 0) {
        return -1;
    }
    return fwrite(record, 1, record_len, stdout) == record_len ? 0 : -1;
}

static void print_report(FILE *stream, const char *path,
                         const message_log_report_t *report) {
    fprintf(stream,
            "path %s\n"
            "records_seen %ld\n"
            "valid_records %ld\n"
            "invalid_records %ld\n"
            "first_invalid_line %ld\n",
            path,
            report->records_seen,
            report->valid_records,
            report->invalid_records,
            report->first_invalid_line);
}

static int scan_log(const char *path, bool recover) {
    FILE *fp;
    char line[MESSAGE_LOG_MAX_LINE];
    long line_no = 0;
    time_t now = time(NULL);
    message_log_report_t report = {0};

    if (!path || path[0] == '\0') {
        fprintf(stderr, "log: invalid path\n");
        return TNT_EXIT_USAGE;
    }

    fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "log: %s: %s\n", path, strerror(errno));
        return TNT_EXIT_ERROR;
    }

    while (fgets(line, sizeof(line), fp)) {
        size_t line_len = strlen(line);
        message_t parsed;
        bool valid = false;

        line_no++;
        report.records_seen++;

        if (line_len >= sizeof(line) - 1 && line[line_len - 1] != '\n') {
            discard_line_remainder(fp);
        } else {
            valid = message_log_parse_record(line, &parsed, now);
        }

        if (valid) {
            report.valid_records++;
            if (recover && print_recovered_record(&parsed) < 0) {
                fclose(fp);
                fprintf(stderr, "log: failed to write recovered output\n");
                return TNT_EXIT_ERROR;
            }
        } else {
            report.invalid_records++;
            if (report.first_invalid_line == 0) {
                report.first_invalid_line = line_no;
            }
        }
    }

    if (ferror(fp)) {
        fclose(fp);
        fprintf(stderr, "log: failed to read %s\n", path);
        return TNT_EXIT_ERROR;
    }
    fclose(fp);

    print_report(recover ? stderr : stdout, path, &report);
    return report.invalid_records == 0 ? TNT_EXIT_OK : TNT_EXIT_ERROR;
}

int message_log_tool_check(const char *path) {
    return scan_log(path, false);
}

int message_log_tool_recover(const char *path) {
    return scan_log(path, true);
}
