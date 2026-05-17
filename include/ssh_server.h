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
    char command_history[16][256];
    int command_history_count;
    int command_history_pos;
    char command_output[2048];
    char exec_command[MAX_EXEC_COMMAND_LEN];
    char ssh_login[MAX_USERNAME_LEN];
    time_t connect_time;
    time_t last_active;
    atomic_bool redraw_pending;
    bool mute_joins;
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

/* Send data to client */
int client_send(client_t *client, const char *data, size_t len);

/* Send formatted string to client */
int client_printf(client_t *client, const char *fmt, ...);

/* Reference counting helpers */
void client_addref(client_t *client);
void client_release(client_t *client);

/* Install the post-bootstrap channel callbacks (window-change, eof, close)
 * that target this client_t.  Caller MUST have already added one
 * client_addref() to keep the client alive across in-flight callback
 * invocations; the matching client_release() happens during cleanup in
 * input_run_session().  Returns 0 on success, -1 on failure (in which
 * case the caller still owns both refs and must release them). */
int client_install_channel_callbacks(client_t *client);

/* Read-only accessor for the server start time (used by exec stats). */
time_t ssh_server_start_time(void);

#endif /* SSH_SERVER_H */
