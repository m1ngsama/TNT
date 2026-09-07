#include "chat_room.h"
#include "cli_text.h"
#include "config_defaults.h"
#include "common.h"
#include "i18n.h"
#include "message.h"
#include "message_log_tool.h"
#include "module_runtime.h"
#include "ssh_server.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

static volatile sig_atomic_t g_shutdown_requested = 0;
static volatile sig_atomic_t g_shutdown_write_fd = -1;

/* The handler only updates sig_atomic_t state and writes to a non-blocking
 * self-pipe.  Both operations are async-signal-safe; teardown happens back in
 * normal control flow after ssh_server_start() has reaped all session work. */
static void signal_handler(int sig) {
    int saved_errno = errno;
    unsigned char byte = (unsigned char)sig;
    int write_fd;

    g_shutdown_requested = sig;
    write_fd = (int)g_shutdown_write_fd;
    if (write_fd >= 0) {
        ssize_t ignored = write(write_fd, &byte, sizeof(byte));
        (void)ignored;
    }
    errno = saved_errno;
}

static int configure_signal_pipe_fd(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    int fd_flags;

    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }

    fd_flags = fcntl(fd, F_GETFD, 0);
    if (fd_flags < 0 || fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC) < 0) {
        return -1;
    }
    return 0;
}

static int setup_signal_handlers(int shutdown_pipe[2]) {
    struct sigaction shutdown_action;
    struct sigaction ignore_action;

    if (pipe(shutdown_pipe) < 0 ||
        configure_signal_pipe_fd(shutdown_pipe[0]) < 0 ||
        configure_signal_pipe_fd(shutdown_pipe[1]) < 0) {
        if (shutdown_pipe[0] >= 0) close(shutdown_pipe[0]);
        if (shutdown_pipe[1] >= 0) close(shutdown_pipe[1]);
        shutdown_pipe[0] = -1;
        shutdown_pipe[1] = -1;
        return -1;
    }

    memset(&shutdown_action, 0, sizeof(shutdown_action));
    shutdown_action.sa_handler = signal_handler;
    sigemptyset(&shutdown_action.sa_mask);
    sigaddset(&shutdown_action.sa_mask, SIGINT);
    sigaddset(&shutdown_action.sa_mask, SIGTERM);

    memset(&ignore_action, 0, sizeof(ignore_action));
    ignore_action.sa_handler = SIG_IGN;
    sigemptyset(&ignore_action.sa_mask);

    g_shutdown_write_fd = shutdown_pipe[1];
    if (sigaction(SIGINT, &shutdown_action, NULL) < 0 ||
        sigaction(SIGTERM, &shutdown_action, NULL) < 0 ||
        sigaction(SIGPIPE, &ignore_action, NULL) < 0) {
        g_shutdown_write_fd = -1;
        close(shutdown_pipe[0]);
        close(shutdown_pipe[1]);
        shutdown_pipe[0] = -1;
        shutdown_pipe[1] = -1;
        return -1;
    }
    return 0;
}

static void close_signal_pipe(int shutdown_pipe[2]) {
    g_shutdown_write_fd = -1;
    if (shutdown_pipe[0] >= 0) close(shutdown_pipe[0]);
    if (shutdown_pipe[1] >= 0) close(shutdown_pipe[1]);
    shutdown_pipe[0] = -1;
    shutdown_pipe[1] = -1;
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

static int set_numeric_env_option(const tnt_int_config_spec_t *spec,
                                  const char *opt_name, const char *value,
                                  ui_lang_t lang) {
    int parsed;

    if (!tnt_config_parse_int(value, spec, &parsed)) {
        fprintf(stderr, cli_text_invalid_value_format(lang), opt_name, value);
        return TNT_EXIT_USAGE;
    }
    if (set_env_option(spec->env_name, value) != 0) {
        return TNT_EXIT_ERROR;
    }
    return TNT_EXIT_OK;
}

static bool require_option_arg(int argc, char **argv, int index,
                               ui_lang_t lang) {
    if (index + 1 >= argc || argv[index + 1][0] == '\0') {
        fprintf(stderr, cli_text_option_requires_arg_format(lang),
                argv[index]);
        return false;
    }
    return true;
}

int main(int argc, char **argv) {
    int port = tnt_config_env_int(&TNT_CONFIG_PORT);
    ui_lang_t lang = i18n_default_ui_lang();
    const char *log_check_path = NULL;
    const char *log_recover_path = NULL;
    int shutdown_pipe[2] = {-1, -1};

    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            int val;
            if (!require_option_arg(argc, argv, i, lang)) {
                return TNT_EXIT_USAGE;
            }
            if (!tnt_config_parse_int(argv[i + 1], &TNT_CONFIG_PORT, &val)) {
                fprintf(stderr, cli_text_invalid_port_format(lang),
                        argv[i + 1]);
                return TNT_EXIT_USAGE;
            }
            port = val;
            i++;
        } else if (strcmp(argv[i], "-d") == 0 ||
                   strcmp(argv[i], "--state-dir") == 0) {
            if (!require_option_arg(argc, argv, i, lang)) {
                return TNT_EXIT_USAGE;
            }
            if (set_env_option("TNT_STATE_DIR", argv[i + 1]) != 0) {
                return TNT_EXIT_ERROR;
            }
            i++;
        } else if (strcmp(argv[i], "--bind") == 0) {
            if (!require_option_arg(argc, argv, i, lang)) {
                return TNT_EXIT_USAGE;
            }
            if (!is_config_token(argv[i + 1])) {
                fprintf(stderr, cli_text_invalid_value_format(lang),
                        argv[i], argv[i + 1]);
                return TNT_EXIT_USAGE;
            }
            if (set_env_option("TNT_BIND_ADDR", argv[i + 1]) != 0) {
                return TNT_EXIT_ERROR;
            }
            i++;
        } else if (strcmp(argv[i], "--public-host") == 0) {
            if (!require_option_arg(argc, argv, i, lang)) {
                return TNT_EXIT_USAGE;
            }
            if (!is_config_token(argv[i + 1])) {
                fprintf(stderr, cli_text_invalid_value_format(lang),
                        argv[i], argv[i + 1]);
                return TNT_EXIT_USAGE;
            }
            if (set_env_option("TNT_PUBLIC_HOST", argv[i + 1]) != 0) {
                return TNT_EXIT_ERROR;
            }
            i++;
        } else if (strcmp(argv[i], "--max-connections") == 0) {
            if (!require_option_arg(argc, argv, i, lang)) {
                return TNT_EXIT_USAGE;
            }
            int rc = set_numeric_env_option(&TNT_CONFIG_MAX_CONNECTIONS,
                                            argv[i], argv[i + 1], lang);
            if (rc != TNT_EXIT_OK) {
                return rc;
            }
            i++;
        } else if (strcmp(argv[i], "--max-conn-per-ip") == 0) {
            if (!require_option_arg(argc, argv, i, lang)) {
                return TNT_EXIT_USAGE;
            }
            int rc = set_numeric_env_option(&TNT_CONFIG_MAX_CONN_PER_IP,
                                            argv[i], argv[i + 1], lang);
            if (rc != TNT_EXIT_OK) {
                return rc;
            }
            i++;
        } else if (strcmp(argv[i], "--max-conn-rate-per-ip") == 0) {
            if (!require_option_arg(argc, argv, i, lang)) {
                return TNT_EXIT_USAGE;
            }
            int rc = set_numeric_env_option(&TNT_CONFIG_MAX_CONN_RATE_PER_IP,
                                            argv[i], argv[i + 1], lang);
            if (rc != TNT_EXIT_OK) {
                return rc;
            }
            i++;
        } else if (strcmp(argv[i], "--rate-limit") == 0) {
            if (!require_option_arg(argc, argv, i, lang)) {
                return TNT_EXIT_USAGE;
            }
            int rc = set_numeric_env_option(&TNT_CONFIG_RATE_LIMIT, argv[i],
                                            argv[i + 1], lang);
            if (rc != TNT_EXIT_OK) {
                return rc;
            }
            i++;
        } else if (strcmp(argv[i], "--idle-timeout") == 0) {
            if (!require_option_arg(argc, argv, i, lang)) {
                return TNT_EXIT_USAGE;
            }
            int rc = set_numeric_env_option(&TNT_CONFIG_IDLE_TIMEOUT, argv[i],
                                            argv[i + 1], lang);
            if (rc != TNT_EXIT_OK) {
                return rc;
            }
            i++;
        } else if (strcmp(argv[i], "--keymap") == 0) {
            if (!require_option_arg(argc, argv, i, lang)) {
                return TNT_EXIT_USAGE;
            }
            /* Round-tripping through the name parser rejects a typo here
             * rather than silently handing every session the wrong keys. */
            if (tnt_keymap_from_name(argv[i + 1], TNT_KEYMAP_VIM) !=
                    tnt_keymap_from_name(argv[i + 1], TNT_KEYMAP_DEFAULT)) {
                fprintf(stderr, cli_text_invalid_value_format(lang), argv[i],
                        argv[i + 1]);
                return TNT_EXIT_USAGE;
            }
            if (set_env_option("TNT_KEYMAP", argv[i + 1]) != 0) {
                return TNT_EXIT_ERROR;
            }
            i++;
        } else if (strcmp(argv[i], "--ssh-log-level") == 0) {
            if (!require_option_arg(argc, argv, i, lang)) {
                return TNT_EXIT_USAGE;
            }
            int rc = set_numeric_env_option(&TNT_CONFIG_SSH_LOG_LEVEL,
                                            argv[i], argv[i + 1], lang);
            if (rc != TNT_EXIT_OK) {
                return rc;
            }
            i++;
        } else if (strcmp(argv[i], "--log-check") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '\0') {
                fprintf(stderr, cli_text_option_requires_arg_format(lang),
                        argv[i]);
                return TNT_EXIT_USAGE;
            }
            log_check_path = argv[++i];
        } else if (strcmp(argv[i], "--log-recover") == 0) {
            if (i + 1 >= argc || argv[i + 1][0] == '\0') {
                fprintf(stderr, cli_text_option_requires_arg_format(lang),
                        argv[i]);
                return TNT_EXIT_USAGE;
            }
            log_recover_path = argv[++i];
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

    if (log_check_path && log_recover_path) {
        fprintf(stderr, cli_text_invalid_value_format(lang),
                "--log-check", "--log-recover");
        return TNT_EXIT_USAGE;
    }
    if (log_check_path) {
        return message_log_tool_check(log_check_path);
    }
    if (log_recover_path) {
        return message_log_tool_recover(log_recover_path);
    }

    /* Setup the self-pipe before installing handlers so a termination signal
     * can always wake the blocking server loop. */
    if (setup_signal_handlers(shutdown_pipe) < 0) {
        perror("Failed to initialize signal handling");
        return TNT_EXIT_ERROR;
    }

    /* Initialize subsystems */
    if (tnt_ensure_state_dir() < 0) {
        fprintf(stderr, "Failed to create state directory: %s\n", tnt_state_dir());
        close_signal_pipe(shutdown_pipe);
        return TNT_EXIT_ERROR;
    }

    message_init();
    if (tnt_module_runtime_init() < 0) {
        fprintf(stderr, "Failed to initialize module runtime\n");
        close_signal_pipe(shutdown_pipe);
        return TNT_EXIT_ERROR;
    }

    /* Create chat room */
    g_room = room_create();
    if (!g_room) {
        fprintf(stderr, "Failed to create chat room\n");
        tnt_module_runtime_shutdown();
        close_signal_pipe(shutdown_pipe);
        return TNT_EXIT_ERROR;
    }

    /* Initialize server */
    if (ssh_server_init(port) < 0) {
        fprintf(stderr, "Failed to initialize server\n");
        tnt_module_runtime_shutdown();
        room_destroy(g_room);
        close_signal_pipe(shutdown_pipe);
        return TNT_EXIT_ERROR;
    }

    /* Start server (blocking) */
    int ret = ssh_server_start(shutdown_pipe[0], &g_shutdown_requested);

    if (g_shutdown_requested) {
        fprintf(stderr, "\nShutting down...\n");
    }

    tnt_module_runtime_shutdown();
    room_destroy(g_room);
    g_room = NULL;
    close_signal_pipe(shutdown_pipe);
    return ret == 0 ? TNT_EXIT_OK : TNT_EXIT_ERROR;
}
