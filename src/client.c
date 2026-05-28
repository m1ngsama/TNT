#include "client.h"
#include "common.h"
#include <libssh/callbacks.h>
#include <libssh/libssh.h>
#include <libssh/server.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int client_send_fail(client_t *client) {
    if (client) {
        client->connected = false;
    }
    return -1;
}

static bool client_is_exec(const client_t *client) {
    return client && (client->exec_command[0] != '\0' ||
                      client->exec_command_too_long);
}

static int client_write_direct_locked(client_t *client, const char *data,
                                      size_t len, size_t budget,
                                      bool fail_on_closed_window) {
    size_t total = 0;

    while (total < len) {
        size_t remaining = len - total;
        uint32_t window = ssh_channel_window_size(client->channel);

        if (window == 0) {
            if (!fail_on_closed_window) {
                break;
            }
            return client_send_fail(client);
        }

        uint32_t chunk = (remaining > 32768) ? 32768 : (uint32_t)remaining;
        if (chunk > window) {
            chunk = window;
        }
        if (budget > 0 && chunk > budget) {
            chunk = (uint32_t)budget;
        }

        int sent = ssh_channel_write(client->channel, data + total, chunk);
        if (sent <= 0) {
            return client_send_fail(client);
        }
        total += (size_t)sent;

        if (budget > 0) {
            budget -= (size_t)sent;
            if (budget == 0) {
                break;
            }
        }
    }

    return (int)total;
}

static int client_flush_output_locked(client_t *client, size_t budget) {
    size_t pending;
    int sent;

    if (!client->outbox || client->outbox_pos >= client->outbox_len) {
        if (client->outbox) {
            client->outbox_pos = 0;
            client->outbox_len = 0;
        }
        return 0;
    }

    pending = client->outbox_len - client->outbox_pos;
    sent = client_write_direct_locked(client, client->outbox + client->outbox_pos,
                                      pending, budget, false);
    if (sent < 0) {
        return -1;
    }

    client->outbox_pos += (size_t)sent;
    if (client->outbox_pos >= client->outbox_len) {
        client->outbox_pos = 0;
        client->outbox_len = 0;
    }

    return 0;
}

static int client_compact_outbox(client_t *client) {
    if (!client->outbox || client->outbox_pos == 0) {
        return 0;
    }

    if (client->outbox_pos < client->outbox_len) {
        memmove(client->outbox, client->outbox + client->outbox_pos,
                client->outbox_len - client->outbox_pos);
        client->outbox_len -= client->outbox_pos;
    } else {
        client->outbox_len = 0;
    }
    client->outbox_pos = 0;
    return 0;
}

static int client_enqueue_output_locked(client_t *client, const char *data,
                                        size_t len) {
    if (len == 0) {
        return 0;
    }

    if (len > CLIENT_OUTBOX_CAPACITY) {
        return client_send_fail(client);
    }

    if (!client->outbox) {
        client->outbox = malloc(CLIENT_OUTBOX_CAPACITY);
        if (!client->outbox) {
            return client_send_fail(client);
        }
        client->outbox_capacity = CLIENT_OUTBOX_CAPACITY;
        client->outbox_len = 0;
        client->outbox_pos = 0;
    }

    client_compact_outbox(client);
    if (client->outbox_len + len > client->outbox_capacity) {
        return client_send_fail(client);
    }

    memcpy(client->outbox + client->outbox_len, data, len);
    client->outbox_len += len;
    return 0;
}

/* Send data to client via SSH channel */
int client_send(client_t *client, const char *data, size_t len) {
    int rc = 0;

    if (!client || !data) return -1;
    if (len == 0) return 0;

    pthread_mutex_lock(&client->io_lock);

    if (!client->connected || !client->channel) {
        pthread_mutex_unlock(&client->io_lock);
        return -1;
    }

    if (client_is_exec(client)) {
        rc = client_write_direct_locked(client, data, len, 0, true);
        if (rc >= 0 && (size_t)rc == len) {
            rc = 0;
        } else if (rc >= 0) {
            rc = client_send_fail(client);
        }
        ssh_blocking_flush(client->session, 1000);
    } else {
        rc = client_enqueue_output_locked(client, data, len);
        if (rc == 0) {
            rc = client_flush_output_locked(client, CLIENT_OUTBOX_FLUSH_BUDGET);
        }
    }

    pthread_mutex_unlock(&client->io_lock);
    return rc;
}

int client_flush_output(client_t *client) {
    int rc;

    if (!client) return 0;

    pthread_mutex_lock(&client->io_lock);

    if (!client->connected || !client->channel) {
        pthread_mutex_unlock(&client->io_lock);
        return -1;
    }

    rc = client_flush_output_locked(client, CLIENT_OUTBOX_FLUSH_BUDGET);
    pthread_mutex_unlock(&client->io_lock);
    return rc;
}

void client_queue_bell(client_t *client) {
    if (!client) return;

    atomic_store(&client->pending_bells, 1);
    client->redraw_pending = true;
}

int client_flush_pending_bells(client_t *client) {
    if (!client) return 0;

    if (atomic_exchange(&client->pending_bells, 0) <= 0) {
        return 0;
    }

    return client_send(client, "\a", 1);
}

void client_addref(client_t *client) {
    if (!client) return;
    pthread_mutex_lock(&client->ref_lock);
    client->ref_count++;
    pthread_mutex_unlock(&client->ref_lock);
}

void client_release(client_t *client) {
    if (!client) return;

    pthread_mutex_lock(&client->ref_lock);
    client->ref_count--;
    int count = client->ref_count;
    pthread_mutex_unlock(&client->ref_lock);

    if (count == 0) {
        /* Safe to free now */
        if (client->channel && client->channel_cb) {
            ssh_remove_channel_callbacks(client->channel, client->channel_cb);
        }
        if (client->channel) {
            if (ssh_channel_is_open(client->channel)) {
                ssh_channel_close(client->channel);
            }
            ssh_channel_free(client->channel);
        }
        if (client->session) {
            ssh_disconnect(client->session);
            ssh_free(client->session);
        }
        if (client->channel_cb) {
            free(client->channel_cb);
        }
        free(client->outbox);
        pthread_mutex_destroy(&client->io_lock);
        pthread_mutex_destroy(&client->whisper_lock);
        pthread_mutex_destroy(&client->ref_lock);
        free(client);
    }
}

void client_release_session(client_t *client) {
    if (!client) return;

    if (client->channel && client->channel_cb) {
        ssh_remove_channel_callbacks(client->channel, client->channel_cb);
    }
    if (client->channel_cb) {
        free(client->channel_cb);
        client->channel_cb = NULL;
    }

    if (client->channel_callback_ref) {
        client->channel_callback_ref = false;
        client_release(client);
    }

    client_release(client);
}

/* Send formatted string to client */
int client_printf(client_t *client, const char *fmt, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    /* Check for buffer overflow or encoding error */
    if (len < 0 || len >= (int)sizeof(buffer)) {
        return -1;
    }

    return client_send(client, buffer, len);
}

static int client_channel_window_change(ssh_session session, ssh_channel channel,
                                        int width, int height,
                                        int pxwidth, int pxheight,
                                        void *userdata) {
    (void)session;
    (void)channel;
    (void)pxwidth;
    (void)pxheight;

    client_t *client = (client_t *)userdata;
    if (!client) {
        return SSH_ERROR;
    }

    int w = width;
    int h = height;
    sanitize_terminal_size(&w, &h);
    client->width = w;
    client->height = h;
    client->redraw_pending = true;
    return SSH_OK;
}

static void client_channel_eof(ssh_session session, ssh_channel channel,
                               void *userdata) {
    (void)session;
    (void)channel;

    client_t *client = (client_t *)userdata;
    if (client) {
        /* Exec clients commonly half-close stdin immediately after sending
         * the command.  Keep stdout usable so the exec handler can return
         * output and an exit status. */
        if (client->exec_command[0] == '\0') {
            client->connected = false;
        }
    }
}

static void client_channel_close(ssh_session session, ssh_channel channel,
                                 void *userdata) {
    (void)session;
    (void)channel;

    client_t *client = (client_t *)userdata;
    if (client) {
        client->connected = false;
    }
}

int client_install_channel_callbacks(client_t *client) {
    if (!client || !client->channel) {
        return -1;
    }

    client_addref(client);
    client->channel_callback_ref = true;

    client->channel_cb = calloc(1, sizeof(struct ssh_channel_callbacks_struct));
    if (!client->channel_cb) {
        client->channel_callback_ref = false;
        client_release(client);
        return -1;
    }

    ssh_callbacks_init(client->channel_cb);
    client->channel_cb->userdata = client;
    client->channel_cb->channel_eof_function = client_channel_eof;
    client->channel_cb->channel_close_function = client_channel_close;
    client->channel_cb->channel_pty_window_change_function =
        client_channel_window_change;

    if (ssh_set_channel_callbacks(client->channel, client->channel_cb) != SSH_OK) {
        free(client->channel_cb);
        client->channel_cb = NULL;
        client->channel_callback_ref = false;
        client_release(client);
        return -1;
    }

    return 0;
}
