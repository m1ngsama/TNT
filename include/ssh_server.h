#ifndef SSH_SERVER_H
#define SSH_SERVER_H

#include "common.h"
#include "chat_room.h"
#include <arpa/inet.h>
#include <libssh/libssh.h>
#include <libssh/server.h>

/* One stored whisper.  Kept per-recipient, not broadcast to the room
 * and not persisted to messages.log.  Inbox is bounded; oldest slides
 * out FIFO. */
#define WHISPER_INBOX_SIZE 16
typedef struct {
    time_t timestamp;
    char from[MAX_USERNAME_LEN];
    char content[MAX_MESSAGE_LEN];
} whisper_t;

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
    /* INSERT mode chat-message history.  Last 16 messages this client
     * sent, oldest first.  Up/Down in INSERT mode walks through it. */
    char insert_history[16][MAX_MESSAGE_LEN];
    int insert_history_count;
    int insert_history_pos;
    char command_output[2048];
    bool show_motd;                  /* command_output holds MOTD text */
    char exec_command[MAX_EXEC_COMMAND_LEN];
    char ssh_login[MAX_USERNAME_LEN];
    time_t connect_time;
    time_t last_active;
    atomic_bool redraw_pending;
    _Atomic int unread_mentions;     /* @-mentions received since last reset */
    _Atomic int unread_whispers;     /* whispers received since last :inbox view */
    /* Per-client whisper inbox.  Pushes serialise on io_lock; readers are
     * the client's own thread inside :inbox handling. */
    whisper_t whisper_inbox[WHISPER_INBOX_SIZE];
    int whisper_inbox_count;
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

/* Read-only accessor for the server start time (used by exec stats). */
time_t ssh_server_start_time(void);

#endif /* SSH_SERVER_H */
