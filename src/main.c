#include "chat_room.h"
#include "cli_text.h"
#include "common.h"
#include "i18n.h"
#include "message.h"
#include "ssh_server.h"
#include <signal.h>
#include <unistd.h>

/* Signal handler: must only call async-signal-safe functions.
 * pthread, malloc, printf, exit() are NOT safe here.
 * Just write a message and call _exit() — OS reclaims all resources. */
static void signal_handler(int sig) {
    (void)sig;
    static const char msg[] = "\nShutting down...\n";
    ssize_t ignored = write(STDERR_FILENO, msg, sizeof(msg) - 1);
    (void)ignored;
    _exit(0);
}

static bool parse_int_arg(const char *value, int min_val, int max_val,
                          int *out) {
    char *end = NULL;
    long val;

    if (!value || value[0] == '\0' || !out) {
        return false;
    }

    val = strtol(value, &end, 10);
    if (!end || *end != '\0' || val < min_val || val > max_val) {
        return false;
    }

    *out = (int)val;
    return true;
}

static bool is_config_token(const char *value) {
    const unsigned char *p = (const unsigned char *)value;

    if (!value || value[0] == '\0') {
        return false;
    }
    while (*p) {
        if (*p <= 32 || *p == 127) {
            return false;
        }
        p++;
    }
    return true;
}

static int set_env_option(const char *name, const char *value) {
    if (setenv(name, value, 1) != 0) {
        perror(name);
        return -1;
    }
    return 0;
}

static int set_numeric_env_option(const char *env_name, const char *opt_name,
                                  const char *value, int min_val,
                                  int max_val, ui_lang_t lang) {
    int parsed;

    if (!parse_int_arg(value, min_val, max_val, &parsed)) {
        fprintf(stderr, cli_text_invalid_value_format(lang), opt_name, value);
        return TNT_EXIT_USAGE;
    }
    if (set_env_option(env_name, value) != 0) {
        return TNT_EXIT_ERROR;
    }
    return TNT_EXIT_OK;
}

int main(int argc, char **argv) {
    int port = DEFAULT_PORT;
    ui_lang_t lang = i18n_default_ui_lang();

    /* Environment provides defaults; command-line flags override it. */
    const char *port_env = getenv("PORT");
    if (port_env && port_env[0] != '\0') {
        char *end;
        long val = strtol(port_env, &end, 10);
        if (*end == '\0' && val > 0 && val <= 65535) {
            port = (int)val;
        }
    }

    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) &&
            i + 1 < argc) {
            int val;
            if (!parse_int_arg(argv[i + 1], 1, 65535, &val)) {
                fprintf(stderr, cli_text_invalid_port_format(lang),
                        argv[i + 1]);
                return TNT_EXIT_USAGE;
            }
            port = val;
            i++;
        } else if ((strcmp(argv[i], "-d") == 0 ||
                    strcmp(argv[i], "--state-dir") == 0) && i + 1 < argc) {
            if (argv[i + 1][0] == '\0') {
                fprintf(stderr, cli_text_invalid_value_format(lang),
                        argv[i], argv[i + 1]);
                return TNT_EXIT_USAGE;
            }
            if (set_env_option("TNT_STATE_DIR", argv[i + 1]) != 0) {
                return TNT_EXIT_ERROR;
            }
            i++;
        } else if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
            if (!is_config_token(argv[i + 1])) {
                fprintf(stderr, cli_text_invalid_value_format(lang),
                        argv[i], argv[i + 1]);
                return TNT_EXIT_USAGE;
            }
            if (set_env_option("TNT_BIND_ADDR", argv[i + 1]) != 0) {
                return TNT_EXIT_ERROR;
            }
            i++;
        } else if (strcmp(argv[i], "--public-host") == 0 && i + 1 < argc) {
            if (!is_config_token(argv[i + 1])) {
                fprintf(stderr, cli_text_invalid_value_format(lang),
                        argv[i], argv[i + 1]);
                return TNT_EXIT_USAGE;
            }
            if (set_env_option("TNT_PUBLIC_HOST", argv[i + 1]) != 0) {
                return TNT_EXIT_ERROR;
            }
            i++;
        } else if (strcmp(argv[i], "--max-connections") == 0 &&
                   i + 1 < argc) {
            int rc = set_numeric_env_option("TNT_MAX_CONNECTIONS", argv[i],
                                            argv[i + 1], 1,
                                            MAX_CONFIGURED_CLIENTS, lang);
            if (rc != TNT_EXIT_OK) {
                return rc;
            }
            i++;
        } else if (strcmp(argv[i], "--max-conn-per-ip") == 0 &&
                   i + 1 < argc) {
            int rc = set_numeric_env_option("TNT_MAX_CONN_PER_IP", argv[i],
                                            argv[i + 1], 1,
                                            MAX_CONFIGURED_CLIENTS, lang);
            if (rc != TNT_EXIT_OK) {
                return rc;
            }
            i++;
        } else if (strcmp(argv[i], "--max-conn-rate-per-ip") == 0 &&
                   i + 1 < argc) {
            int rc = set_numeric_env_option("TNT_MAX_CONN_RATE_PER_IP",
                                            argv[i], argv[i + 1], 1,
                                            MAX_CONFIGURED_CLIENTS, lang);
            if (rc != TNT_EXIT_OK) {
                return rc;
            }
            i++;
        } else if (strcmp(argv[i], "--rate-limit") == 0 && i + 1 < argc) {
            int rc = set_numeric_env_option("TNT_RATE_LIMIT", argv[i],
                                            argv[i + 1], 0, 1, lang);
            if (rc != TNT_EXIT_OK) {
                return rc;
            }
            i++;
        } else if (strcmp(argv[i], "--idle-timeout") == 0 && i + 1 < argc) {
            int rc = set_numeric_env_option("TNT_IDLE_TIMEOUT", argv[i],
                                            argv[i + 1], 0, 86400, lang);
            if (rc != TNT_EXIT_OK) {
                return rc;
            }
            i++;
        } else if (strcmp(argv[i], "--ssh-log-level") == 0 && i + 1 < argc) {
            int rc = set_numeric_env_option("TNT_SSH_LOG_LEVEL", argv[i],
                                            argv[i + 1], 0, 4, lang);
            if (rc != TNT_EXIT_OK) {
                return rc;
            }
            i++;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("tnt %s\n", TNT_VERSION);
            return TNT_EXIT_OK;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            char output[2048] = {0};
            size_t pos = 0;

            cli_text_append_help(output, sizeof(output), &pos, argv[0], lang);
            fputs(output, stdout);
            return TNT_EXIT_OK;
        } else {
            fprintf(stderr, cli_text_unknown_option_format(lang), argv[i]);
            fprintf(stderr, cli_text_short_usage_format(lang), argv[0]);
            return TNT_EXIT_USAGE;
        }
    }

    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    /* Initialize subsystems */
    if (tnt_ensure_state_dir() < 0) {
        fprintf(stderr, "Failed to create state directory: %s\n", tnt_state_dir());
        return TNT_EXIT_ERROR;
    }

    message_init();

    /* Create chat room */
    g_room = room_create();
    if (!g_room) {
        fprintf(stderr, "Failed to create chat room\n");
        return TNT_EXIT_ERROR;
    }

    /* Initialize server */
    if (ssh_server_init(port) < 0) {
        fprintf(stderr, "Failed to initialize server\n");
        room_destroy(g_room);
        return TNT_EXIT_ERROR;
    }

    /* Start server (blocking) */
    int ret = ssh_server_start(0);

    room_destroy(g_room);
    return ret;
}
