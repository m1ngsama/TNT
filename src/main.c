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

int main(int argc, char **argv) {
    int port = DEFAULT_PORT;
    help_lang_t lang = i18n_default_lang();

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
            char *end;
            long val = strtol(argv[i + 1], &end, 10);
            if (*end != '\0' || val <= 0 || val > 65535) {
                fprintf(stderr, cli_text_invalid_port_format(lang),
                        argv[i + 1]);
                return 1;
            }
            port = (int)val;
            i++;
        } else if ((strcmp(argv[i], "-d") == 0 ||
                    strcmp(argv[i], "--state-dir") == 0) && i + 1 < argc) {
            if (setenv("TNT_STATE_DIR", argv[i + 1], 1) != 0) {
                perror("setenv TNT_STATE_DIR");
                return 1;
            }
            i++;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("tnt %s\n", TNT_VERSION);
            return 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            char output[2048] = {0};
            size_t pos = 0;

            cli_text_append_help(output, sizeof(output), &pos, argv[0], lang);
            fputs(output, stdout);
            return 0;
        } else {
            fprintf(stderr, cli_text_unknown_option_format(lang), argv[i]);
            fprintf(stderr, cli_text_short_usage_format(lang), argv[0]);
            return 1;
        }
    }

    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    /* Initialize subsystems */
    if (tnt_ensure_state_dir() < 0) {
        fprintf(stderr, "Failed to create state directory: %s\n", tnt_state_dir());
        return 1;
    }

    message_init();

    /* Create chat room */
    g_room = room_create();
    if (!g_room) {
        fprintf(stderr, "Failed to create chat room\n");
        return 1;
    }

    /* Initialize server */
    if (ssh_server_init(port) < 0) {
        fprintf(stderr, "Failed to initialize server\n");
        room_destroy(g_room);
        return 1;
    }

    /* Start server (blocking) */
    int ret = ssh_server_start(0);

    room_destroy(g_room);
    return ret;
}
