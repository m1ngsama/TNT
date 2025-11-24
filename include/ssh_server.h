#ifndef SSH_SERVER_H
#define SSH_SERVER_H

#include "common.h"
#include "chat_room.h"
#include <libssh/libssh.h>
#include <libssh/server.h>

/* Client connection structure */
typedef struct client {
    int fd;                          /* Socket file descriptor (not used with SSH) */
    ssh_session session;             /* SSH session */
    ssh_channel channel;             /* SSH channel */
    char username[MAX_USERNAME_LEN];
    int width;
    int height;
    client_mode_t mode;
    help_lang_t help_lang;
    int scroll_pos;
    int help_scroll_pos;
    bool show_help;
    char command_input[256];
    char command_output[2048];
    pthread_t thread;
    bool connected;
} client_t;

/* Initialize SSH server */
int ssh_server_init(int port);

/* Start SSH server (blocking) */
int ssh_server_start(int listen_fd);

/* Handle client session */
void* client_handle_session(void *arg);

/* Send data to client */
int client_send(client_t *client, const char *data, size_t len);

/* Send formatted string to client */
int client_printf(client_t *client, const char *fmt, ...);

#endif /* SSH_SERVER_H */
