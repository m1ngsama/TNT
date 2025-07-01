#include "common.h"
#include "ssh_server.h"
#include "chat_room.h"
#include "message.h"
#include <signal.h>
#include <unistd.h>

static int g_listen_fd = -1;

/* Signal handler for graceful shutdown */
static void signal_handler(int sig) {
    (void)sig;
    if (g_listen_fd >= 0) {
        close(g_listen_fd);
    }
    if (g_room) {
        room_destroy(g_room);
    }
    printf("\nShutting down...\n");
    exit(0);
}

int main(int argc, char **argv) {
    int port = DEFAULT_PORT;

    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("TNT - Terminal Network Talk\n");
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  -p PORT    Listen on PORT (default: %d)\n", DEFAULT_PORT);
            printf("  -h         Show this help\n");
            return 0;
        }
    }

    /* Check environment variable for port */
    const char *port_env = getenv("PORT");
    if (port_env) {
        port = atoi(port_env);
    }

    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    /* Initialize subsystems */
    message_init();

    /* Create chat room */
    g_room = room_create();
    if (!g_room) {
        fprintf(stderr, "Failed to create chat room\n");
        return 1;
    }

    /* Initialize server */
    g_listen_fd = ssh_server_init(port);
    if (g_listen_fd < 0) {
        fprintf(stderr, "Failed to initialize server\n");
        room_destroy(g_room);
        return 1;
    }

    /* Start server (blocking) */
    int ret = ssh_server_start(g_listen_fd);

    /* Cleanup */
    close(g_listen_fd);
    room_destroy(g_room);

    return ret;
}
