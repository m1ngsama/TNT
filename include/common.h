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

#include "config_defaults.h"

/* Project Metadata */
#define TNT_VERSION "1.2.0"

/* Public process/exec exit statuses.  TNT follows the common sysexits(3)
 * convention for usage errors while keeping runtime failures portable. */
#define TNT_EXIT_OK 0
#define TNT_EXIT_ERROR 1
#define TNT_EXIT_USAGE 64
#define TNT_EXIT_UNAVAILABLE 69
#define TNT_EXIT_CONFIG 78

/* Configuration constants */
#define MAX_MESSAGES 100
#define MAX_USERNAME_LEN 64
#define MAX_MESSAGE_LEN 1024
#define MAX_EXEC_COMMAND_LEN 1024
#define MAX_COMMAND_OUTPUT_LEN 8192
#define CLIENT_OUTBOX_CAPACITY (128 * 1024)
#define CLIENT_OUTBOX_FLUSH_BUDGET 32768
#define LOG_FILE "messages.log"
#define MAX_LOG_SIZE (10 * 1024 * 1024)  /* 10 MiB */
#define HOST_KEY_FILE "host_key"
#define TNT_DEFAULT_STATE_DIR "."

/* Backward-compatible names for older modules while config_defaults owns the
 * actual runtime defaults and accepted ranges. */
#define DEFAULT_PORT TNT_DEFAULT_PORT
#define DEFAULT_MAX_CLIENTS TNT_DEFAULT_MAX_CONNECTIONS
#define MAX_CONFIGURED_CLIENTS TNT_MAX_CONFIGURED_CLIENTS
#define DEFAULT_IDLE_TIMEOUT TNT_DEFAULT_IDLE_TIMEOUT

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
    MODE_COMMAND
} client_mode_t;

/* UI language */
typedef enum {
    UI_LANG_EN,
    UI_LANG_ZH,
    UI_LANG_COUNT
} ui_lang_t;

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

/* Reject usernames containing shell metacharacters, control characters, or
 * a leading space/dot/dash.  Used by username read, exec post (SSH login as
 * author), and the :nick command. */
bool is_valid_username(const char *username);

/* Clamp a terminal size to sensible bounds (1..500 cols, 1..200 rows).
 * Replaces zero/negative/oversize values with 80x24.  Used by the PTY
 * request callback and the window-change callback. */
void sanitize_terminal_size(int *width, int *height);

#endif /* COMMON_H */
