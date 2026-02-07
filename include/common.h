#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

/* Project Metadata */
#define TNT_VERSION "1.0.0"

/* Configuration constants */
#define DEFAULT_PORT 2222
#define MAX_MESSAGES 100
#define MAX_USERNAME_LEN 64
#define MAX_MESSAGE_LEN 1024
#define MAX_CLIENTS 64
#define LOG_FILE "messages.log"
#define HOST_KEY_FILE "host_key"

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

#endif /* COMMON_H */
