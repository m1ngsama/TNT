#ifndef SSH_SERVER_H
#define SSH_SERVER_H

#include "common.h"
#include "chat_room.h"
#include <arpa/inet.h>
#include <libssh/libssh.h>
#include <libssh/server.h>

/* Client connection structure */
typedef struct client {
    ssh_session session;             /* SSH session */
    ssh_channel channel;             /* SSH channel */
    char username[MAX_USERNAME_LEN];
    char client_ip[INET6_ADDRSTRLEN];
    _Atomic int width;
    _Atomic int height;
    client_mode_t mode;
    help_lang_t help_lang;
    int scroll_pos;
    int help_scroll_pos;
    bool show_help;
    char command_input[256];
    char command_output[2048];
    char exec_command[MAX_EXEC_COMMAND_LEN];
    char ssh_login[MAX_USERNAME_LEN];
    atomic_bool redraw_pending;
    pthread_t thread;
    atomic_bool connected;
    int ref_count;                   /* Reference count for safe cleanup */
    pthread_mutex_t ref_lock;        /* Lock for ref_count */
    pthread_mutex_t io_lock;         /* Serialize SSH channel writes */
    struct ssh_channel_callbacks_struct *channel_cb;
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

/* Reference counting helpers */
void client_addref(client_t *client);
void client_release(client_t *client);

#endif /* SSH_SERVER_H */
