#include "bootstrap.h"
#include "client.h"
#include "common.h"
#include "input.h"
#include "ratelimit.h"
#include <arpa/inet.h>
#include <errno.h>
#include <libssh/callbacks.h>
#include <libssh/libssh.h>
#include <libssh/server.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

/* Per-connection bootstrap state.  Kept private to this translation unit:
 * its lifetime ends inside bootstrap_run() once a client_t takes over. */
typedef struct {
    char client_ip[INET6_ADDRSTRLEN];
    char requested_user[MAX_USERNAME_LEN];
    int pty_width;
    int pty_height;
    char exec_command[MAX_EXEC_COMMAND_LEN];
    bool auth_success;
    int auth_attempts;
    bool channel_ready;  /* Set when shell/exec request received */
    ssh_channel channel;  /* Channel created in callback */
    struct ssh_channel_callbacks_struct *channel_cb;  /* Channel callbacks */
} session_context_t;

/* Configured access token; empty string means "no auth required". */
static char g_access_token[256] = "";

void bootstrap_init(void) {
    const char *token_env = getenv("TNT_ACCESS_TOKEN");
    if (token_env != NULL) {
        strncpy(g_access_token, token_env, sizeof(g_access_token) - 1);
        g_access_token[sizeof(g_access_token) - 1] = '\0';
    } else {
        g_access_token[0] = '\0';
    }
}

void bootstrap_peer_ip(ssh_session session, char *ip_buf, size_t buf_size) {
    int fd = ssh_get_fd(session);
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);

    if (getpeername(fd, (struct sockaddr *)&addr, &addr_len) == 0) {
        if (addr.ss_family == AF_INET) {
            struct sockaddr_in *s = (struct sockaddr_in *)&addr;
            inet_ntop(AF_INET, &s->sin_addr, ip_buf, buf_size);
        } else if (addr.ss_family == AF_INET6) {
            struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;
            inet_ntop(AF_INET6, &s->sin6_addr, ip_buf, buf_size);
        } else {
            strncpy(ip_buf, "unknown", buf_size - 1);
        }
    } else {
        strncpy(ip_buf, "unknown", buf_size - 1);
    }
    ip_buf[buf_size - 1] = '\0';
}

/* Constant-time string comparison to prevent timing side-channel attacks.
 * Always iterates over the full length of the secret (b) to avoid leaking
 * its length.  When the input (a) is shorter, compares against zero bytes;
 * the length mismatch is folded into the result separately.
 *
 * Note: the length-diff is accumulated in size_t to avoid the bug where a
 * narrower type (e.g. unsigned char) would collapse pairs like (300, 44) to
 * 0 because 300 ^ 44 == 256 ^ (44 ^ 44) == 256 which truncates to 0. */
static bool constant_time_strcmp(const char *a, const char *b) {
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    volatile size_t length_diff = len_a ^ len_b;
    volatile unsigned char byte_diff = 0;
    for (size_t i = 0; i < len_b; i++) {
        unsigned char ca = (i < len_a) ? (unsigned char)a[i] : 0;
        byte_diff |= ca ^ (unsigned char)b[i];
    }
    return length_diff == 0 && byte_diff == 0;
}

/* Password authentication callback */
static int auth_password(ssh_session session, const char *user,
                         const char *password, void *userdata) {
    session_context_t *ctx = (session_context_t *)userdata;

    if (user && user[0] != '\0') {
        strncpy(ctx->requested_user, user, sizeof(ctx->requested_user) - 1);
        ctx->requested_user[sizeof(ctx->requested_user) - 1] = '\0';
    }

    ctx->auth_attempts++;

    /* Limit auth attempts */
    if (ctx->auth_attempts > 3) {
        ratelimit_record_auth_failure(ctx->client_ip);
        fprintf(stderr, "Too many auth attempts from %s\n", ctx->client_ip);
        ssh_disconnect(session);
        return SSH_AUTH_DENIED;
    }

    /* If access token is configured, require it */
    if (g_access_token[0] != '\0') {
        if (password && constant_time_strcmp(password, g_access_token)) {
            /* Token matches */
            ctx->auth_success = true;
            return SSH_AUTH_SUCCESS;
        } else {
            /* Wrong token — IP blocking handles brute force, no sleep needed here
             * (sleeping in a libssh callback blocks the entire accept loop). */
            ratelimit_record_auth_failure(ctx->client_ip);
            return SSH_AUTH_DENIED;
        }
    } else {
        /* No token configured, accept any password */
        ctx->auth_success = true;
        return SSH_AUTH_SUCCESS;
    }
}

/* Passwordless (none) authentication callback */
static int auth_none(ssh_session session, const char *user, void *userdata) {
    (void)session;  /* Unused */
    session_context_t *ctx = (session_context_t *)userdata;

    if (user && user[0] != '\0') {
        strncpy(ctx->requested_user, user, sizeof(ctx->requested_user) - 1);
        ctx->requested_user[sizeof(ctx->requested_user) - 1] = '\0';
    }

    /* If access token is configured, reject passwordless */
    if (g_access_token[0] != '\0') {
        return SSH_AUTH_DENIED;
    } else {
        /* No token configured, allow passwordless */
        ctx->auth_success = true;
        return SSH_AUTH_SUCCESS;
    }
}

/* Public key authentication callback */
static int auth_pubkey(ssh_session session, const char *user,
                       struct ssh_key_struct *pubkey, char signature_state,
                       void *userdata) {
    (void)session;
    (void)pubkey;
    session_context_t *ctx = (session_context_t *)userdata;

    if (user && user[0] != '\0') {
        strncpy(ctx->requested_user, user, sizeof(ctx->requested_user) - 1);
        ctx->requested_user[sizeof(ctx->requested_user) - 1] = '\0';
    }

    /* Reject if access token is required (pubkey auth not supported with tokens) */
    if (g_access_token[0] != '\0') {
        return SSH_AUTH_DENIED;
    }

    /* SSH_PUBLICKEY_STATE_NONE = key offer (no signature yet).
     * Return SUCCESS to tell libssh "I accept this key, verify the signature."
     * SSH_PUBLICKEY_STATE_VALID = signature verified by libssh. */
    if (signature_state != SSH_PUBLICKEY_STATE_VALID) {
        return SSH_AUTH_SUCCESS;
    }

    ctx->auth_success = true;
    return SSH_AUTH_SUCCESS;
}

static void destroy_session_context(session_context_t *ctx) {
    if (!ctx) {
        return;
    }

    if (ctx->channel_cb) {
        free(ctx->channel_cb);
    }

    free(ctx);
}

static void cleanup_failed_session(ssh_session session, session_context_t *ctx) {
    if (ctx && ctx->channel) {
        if (ctx->channel_cb) {
            ssh_remove_channel_callbacks(ctx->channel, ctx->channel_cb);
        }
        ssh_channel_close(ctx->channel);
        ssh_channel_free(ctx->channel);
        ctx->channel = NULL;
    }

    if (session) {
        ssh_disconnect(session);
        ssh_free(session);
    }

    if (ctx) {
        ratelimit_release_ip(ctx->client_ip);
    }
    destroy_session_context(ctx);
    ratelimit_decrement_total();
}

static void setup_session_channel_callbacks(ssh_channel channel,
                                            session_context_t *ctx);

/* Channel open callback */
static ssh_channel channel_open_request_session(ssh_session session, void *userdata) {
    session_context_t *ctx = (session_context_t *)userdata;
    ssh_channel channel;

    channel = ssh_channel_new(session);
    if (channel == NULL) {
        return NULL;
    }

    /* Store channel in context for main loop */
    ctx->channel = channel;

    /* Set up channel-specific callbacks (PTY, shell, exec) */
    setup_session_channel_callbacks(channel, ctx);

    return channel;
}

/* PTY request callback */
static int channel_pty_request(ssh_session session, ssh_channel channel,
                               const char *term, int width, int height,
                               int pxwidth, int pxheight, void *userdata) {
    (void)session;   /* Unused */
    (void)channel;   /* Unused */
    (void)term;      /* Unused */
    (void)pxwidth;   /* Unused */
    (void)pxheight;  /* Unused */

    session_context_t *ctx = (session_context_t *)userdata;

    /* Store terminal dimensions */
    ctx->pty_width = width;
    ctx->pty_height = height;

    sanitize_terminal_size(&ctx->pty_width, &ctx->pty_height);

    return SSH_OK;
}

static int channel_pty_window_change(ssh_session session, ssh_channel channel,
                                     int width, int height,
                                     int pxwidth, int pxheight,
                                     void *userdata) {
    (void)session;
    (void)channel;
    (void)pxwidth;
    (void)pxheight;

    session_context_t *ctx = (session_context_t *)userdata;

    ctx->pty_width = width;
    ctx->pty_height = height;
    sanitize_terminal_size(&ctx->pty_width, &ctx->pty_height);

    return SSH_OK;
}

/* Shell request callback */
static int channel_shell_request(ssh_session session, ssh_channel channel,
                                 void *userdata) {
    (void)session;  /* Unused */
    (void)channel;  /* Unused */

    session_context_t *ctx = (session_context_t *)userdata;

    /* Mark channel as ready */
    ctx->channel_ready = true;

    /* Accept shell request */
    return SSH_OK;
}

/* Exec request callback */
static int channel_exec_request(ssh_session session, ssh_channel channel,
                                const char *command, void *userdata) {
    (void)session;  /* Unused */
    (void)channel;  /* Unused */

    session_context_t *ctx = (session_context_t *)userdata;

    /* Store exec command */
    if (command) {
        strncpy(ctx->exec_command, command, sizeof(ctx->exec_command) - 1);
        ctx->exec_command[sizeof(ctx->exec_command) - 1] = '\0';
    }

    /* Mark channel as ready */
    ctx->channel_ready = true;

    return SSH_OK;
}

/* Set up channel callbacks */
static void setup_session_channel_callbacks(ssh_channel channel,
                                            session_context_t *ctx) {
    /* Allocate channel callbacks on heap to persist */
    ctx->channel_cb = calloc(1, sizeof(struct ssh_channel_callbacks_struct));
    if (!ctx->channel_cb) {
        return;
    }

    ssh_callbacks_init(ctx->channel_cb);

    ctx->channel_cb->userdata = ctx;
    ctx->channel_cb->channel_pty_request_function = channel_pty_request;
    ctx->channel_cb->channel_shell_request_function = channel_shell_request;
    ctx->channel_cb->channel_pty_window_change_function = channel_pty_window_change;
    ctx->channel_cb->channel_exec_request_function = channel_exec_request;

    ssh_set_channel_callbacks(channel, ctx->channel_cb);
}

void *bootstrap_run(void *arg) {
    accepted_session_t *accepted = (accepted_session_t *)arg;
    ssh_session session;
    session_context_t *ctx = NULL;
    ssh_event event = NULL;
    struct ssh_server_callbacks_struct server_cb;
    ssh_channel channel;
    client_t *client = NULL;
    bool timed_out = false;
    time_t start_time;
    char accepted_ip[INET6_ADDRSTRLEN] = "";

    if (!accepted) {
        return NULL;
    }

    session = accepted->session;
    if (accepted->client_ip[0] != '\0') {
        snprintf(accepted_ip, sizeof(accepted_ip), "%s", accepted->client_ip);
    }
    free(accepted);

    ctx = calloc(1, sizeof(session_context_t));
    if (!ctx) {
        ratelimit_release_ip(accepted_ip);
        ssh_disconnect(session);
        ssh_free(session);
        ratelimit_decrement_total();
        return NULL;
    }

    if (accepted_ip[0] != '\0') {
        snprintf(ctx->client_ip, sizeof(ctx->client_ip), "%s", accepted_ip);
    } else {
        bootstrap_peer_ip(session, ctx->client_ip, sizeof(ctx->client_ip));
    }
    ctx->pty_width = 80;
    ctx->pty_height = 24;
    ctx->exec_command[0] = '\0';
    ctx->requested_user[0] = '\0';
    ctx->auth_success = false;
    ctx->auth_attempts = 0;
    ctx->channel_ready = false;
    ctx->channel = NULL;
    ctx->channel_cb = NULL;

    memset(&server_cb, 0, sizeof(server_cb));
    ssh_callbacks_init(&server_cb);
    server_cb.userdata = ctx;
    server_cb.auth_password_function = auth_password;
    server_cb.auth_none_function = auth_none;
    server_cb.auth_pubkey_function = auth_pubkey;
    server_cb.channel_open_request_session_function = channel_open_request_session;
    ssh_set_server_callbacks(session, &server_cb);

    if (ssh_handle_key_exchange(session) != SSH_OK) {
        fprintf(stderr, "Key exchange failed from %s: %s\n",
                ctx->client_ip, ssh_get_error(session));
        cleanup_failed_session(session, ctx);
        return NULL;
    }

    event = ssh_event_new();
    if (!event) {
        fprintf(stderr, "Failed to create SSH event for %s\n", ctx->client_ip);
        cleanup_failed_session(session, ctx);
        return NULL;
    }

    if (ssh_event_add_session(event, session) != SSH_OK) {
        fprintf(stderr, "Failed to add session to event loop for %s\n",
                ctx->client_ip);
        ssh_event_free(event);
        cleanup_failed_session(session, ctx);
        return NULL;
    }

    start_time = time(NULL);
    while ((!ctx->auth_success || ctx->channel == NULL || !ctx->channel_ready) &&
           !timed_out) {
        int rc = ssh_event_dopoll(event, 1000);

        if (rc == SSH_ERROR) {
            fprintf(stderr, "Event poll error from %s: %s\n",
                    ctx->client_ip, ssh_get_error(session));
            break;
        }

        if (time(NULL) - start_time > 10) {
            timed_out = true;
        }
    }

    ssh_event_free(event);
    event = NULL;

    if (!ctx->auth_success) {
        fprintf(stderr, "Authentication failed or timed out from %s\n",
                ctx->client_ip);
        cleanup_failed_session(session, ctx);
        return NULL;
    }

    channel = ctx->channel;
    if (!channel || !ctx->channel_ready || timed_out) {
        fprintf(stderr, "Failed to open/setup channel from %s\n",
                ctx->client_ip);
        cleanup_failed_session(session, ctx);
        return NULL;
    }

    client = calloc(1, sizeof(client_t));
    if (!client) {
        cleanup_failed_session(session, ctx);
        return NULL;
    }

    client->session = session;
    client->channel = channel;
    int init_w = ctx->pty_width;
    int init_h = ctx->pty_height;
    sanitize_terminal_size(&init_w, &init_h);
    client->width = init_w;
    client->height = init_h;
    client->ref_count = 1;
    pthread_mutex_init(&client->ref_lock, NULL);
    pthread_mutex_init(&client->io_lock, NULL);

    if (ctx->requested_user[0] != '\0') {
        strncpy(client->ssh_login, ctx->requested_user,
                sizeof(client->ssh_login) - 1);
        client->ssh_login[sizeof(client->ssh_login) - 1] = '\0';
    }
    if (ctx->client_ip[0] != '\0') {
        snprintf(client->client_ip, sizeof(client->client_ip), "%s",
                 ctx->client_ip);
    }
    if (ctx->exec_command[0] != '\0') {
        strncpy(client->exec_command, ctx->exec_command,
                sizeof(client->exec_command) - 1);
        client->exec_command[sizeof(client->exec_command) - 1] = '\0';
    }

    /* Add a ref for the channel callbacks (eof/close/window_change) so the
     * client_t outlives any in-flight callback invocation. */
    client_addref(client);

    if (client_install_channel_callbacks(client) < 0) {
        /* Nullify session/channel ownership so client_release won't
         * double-free what cleanup_failed_session is about to free. */
        client->session = NULL;
        client->channel = NULL;
        client_release(client);  /* drop the callback ref (2 → 1) */
        client_release(client);  /* drop the main ref (1 → 0, frees client) */
        cleanup_failed_session(session, ctx);
        return NULL;
    }

    if (ctx->channel_cb) {
        ssh_remove_channel_callbacks(channel, ctx->channel_cb);
        free(ctx->channel_cb);
        ctx->channel_cb = NULL;
    }
    destroy_session_context(ctx);

    input_run_session(client);
    return NULL;
}
