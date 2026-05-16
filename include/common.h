#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <time.h>
#include <limits.h>
#include <pthread.h>

/* Project Metadata */
#define TNT_VERSION "1.0.0"

/* Configuration constants */
#define DEFAULT_PORT 2222
#define MAX_MESSAGES 100
#define MAX_USERNAME_LEN 64
#define MAX_MESSAGE_LEN 1024
#define MAX_EXEC_COMMAND_LEN 1024
#define MAX_CLIENTS 64
#define LOG_FILE "messages.log"
#define MAX_LOG_SIZE (10 * 1024 * 1024)  /* 10 MiB */
#define HOST_KEY_FILE "host_key"
#define TNT_DEFAULT_STATE_DIR "."
#define DEFAULT_IDLE_TIMEOUT 1800  /* 30 minutes */

/* ANSI color codes */
#define ANSI_RESET "\033[0m"
#define ANSI_BOLD "\033[1m"
#define ANSI_REVERSE "\033[7m"
#define ANSI_CLEAR "\033[2J"
#define ANSI_HOME "\033[H"
#define ANSI_CLEAR_LINE "\033[K"

/* Operating modes */
typedef enum {
    MODE_INSERT,
    MODE_NORMAL,
    MODE_COMMAND,
    MODE_HELP
} client_mode_t;

/* Help language */
typedef enum {
    LANG_EN,
    LANG_ZH
} help_lang_t;

/* Runtime helpers */
const char* tnt_state_dir(void);
int tnt_ensure_state_dir(void);
int tnt_state_path(char *buffer, size_t buf_size, const char *filename);

/* Bounded string buffer builders. Both append to `buffer[*pos..]`, advance
 * `*pos`, and always keep the buffer NUL-terminated. They never write past
 * `buf_size - 1` and become no-ops once the buffer is full. */
void buffer_append_bytes(char *buffer, size_t buf_size, size_t *pos,
                         const char *data, size_t len);
void buffer_appendf(char *buffer, size_t buf_size, size_t *pos,
                    const char *fmt, ...);

/* Parse an integer from `getenv(name)`, clamping accepted values to
 * [min_val, max_val].  Returns `fallback` when the variable is unset, empty,
 * non-numeric, or out of range. */
int env_int(const char *name, int fallback, int min_val, int max_val);

#endif /* COMMON_H */
