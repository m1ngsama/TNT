#include "common.h"
#include "ssh_server.h"
#include "chat_room.h"
#include "message.h"
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
                fprintf(stderr, "Invalid port: %s\n", argv[i + 1]);
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
            printf("tnt %s - anonymous SSH chat server\n\n", TNT_VERSION);
            printf("Usage: %s [options]\n\n", argv[0]);
            printf("Options:\n");
            printf("  -p, --port PORT       Listen on PORT (default: %d)\n", DEFAULT_PORT);
            printf("  -d, --state-dir DIR   Store host key and logs in DIR\n");
            printf("  -V, --version         Show version\n");
            printf("  -h, --help            Show this help\n");
            printf("\nEnvironment:\n");
            printf("  PORT                  Default listening port\n");
            printf("  TNT_STATE_DIR         State directory\n");
            printf("  TNT_ACCESS_TOKEN      Require this password for SSH auth\n");
            printf("  TNT_MAX_CONNECTIONS   Global connection limit (default: 64)\n");
            printf("  TNT_RATE_LIMIT        Set to 0 to disable rate limiting\n");
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s [-p PORT] [-d DIR] [-h]\n", argv[0]);
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
