#include "chat_room.h"
#include "ssh_server.h"
#include "tui.h"
#include <unistd.h>

/* Global chat room instance */
chat_room_t *g_room = NULL;

/* Initialize chat room */
chat_room_t* room_create(void) {
    chat_room_t *room = calloc(1, sizeof(chat_room_t));
    if (!room) return NULL;

    pthread_rwlock_init(&room->lock, NULL);

    room->client_capacity = MAX_CLIENTS;
    room->clients = calloc(room->client_capacity, sizeof(client_t*));
    if (!room->clients) {
        free(room);
        return NULL;
    }

    /* Load messages from file */
    room->message_count = message_load(&room->messages, MAX_MESSAGES);

    return room;
}

/* Destroy chat room */
void room_destroy(chat_room_t *room) {
    if (!room) return;

    pthread_rwlock_wrlock(&room->lock);

    free(room->clients);
    free(room->messages);

    pthread_rwlock_unlock(&room->lock);
    pthread_rwlock_destroy(&room->lock);

    free(room);
}

/* Add client to room */
int room_add_client(chat_room_t *room, client_t *client) {
    pthread_rwlock_wrlock(&room->lock);

    if (room->client_count >= room->client_capacity) {
        pthread_rwlock_unlock(&room->lock);
        return -1;
    }

    room->clients[room->client_count++] = client;

    pthread_rwlock_unlock(&room->lock);
    return 0;
}

/* Remove client from room */
void room_remove_client(chat_room_t *room, client_t *client) {
    pthread_rwlock_wrlock(&room->lock);

    for (int i = 0; i < room->client_count; i++) {
        if (room->clients[i] == client) {
            /* Shift remaining clients */
            for (int j = i; j < room->client_count - 1; j++) {
                room->clients[j] = room->clients[j + 1];
            }
            room->client_count--;
            break;
        }
    }

    pthread_rwlock_unlock(&room->lock);
}

/* Broadcast message to all clients */
void room_broadcast(chat_room_t *room, const message_t *msg) {
    pthread_rwlock_wrlock(&room->lock);

    /* Add to history */
    room_add_message(room, msg);

    /* Get copy of client list and increment ref counts */
    client_t **clients_copy = calloc(room->client_count, sizeof(client_t*));
    int count = room->client_count;
    memcpy(clients_copy, room->clients, count * sizeof(client_t*));

    /* Increment reference count for each client */
    for (int i = 0; i < count; i++) {
        pthread_mutex_lock(&clients_copy[i]->ref_lock);
        clients_copy[i]->ref_count++;
        pthread_mutex_unlock(&clients_copy[i]->ref_lock);
    }

    pthread_rwlock_unlock(&room->lock);

    /* Render to each client (outside of lock) */
    for (int i = 0; i < count; i++) {
        client_t *client = clients_copy[i];
        if (client->connected && !client->show_help &&
            client->command_output[0] == '\0') {
            tui_render_screen(client);
        }

        /* Decrement reference count and free if needed */
        pthread_mutex_lock(&client->ref_lock);
        client->ref_count--;
        int ref = client->ref_count;
        pthread_mutex_unlock(&client->ref_lock);

        if (ref == 0) {
            /* Safe to free now */
            if (client->channel) {
                ssh_channel_close(client->channel);
                ssh_channel_free(client->channel);
            }
            if (client->session) {
                ssh_disconnect(client->session);
                ssh_free(client->session);
            }
            pthread_mutex_destroy(&client->ref_lock);
            free(client);
        }
    }

    free(clients_copy);
}

/* Add message to room history */
void room_add_message(chat_room_t *room, const message_t *msg) {
    /* Caller should hold write lock */

    if (room->message_count >= MAX_MESSAGES) {
        /* Shift messages to make room */
        memmove(&room->messages[0], &room->messages[1],
                (MAX_MESSAGES - 1) * sizeof(message_t));
        room->message_count = MAX_MESSAGES - 1;
    }

    room->messages[room->message_count++] = *msg;
}

/* Get message by index */
const message_t* room_get_message(chat_room_t *room, int index) {
    pthread_rwlock_rdlock(&room->lock);

    const message_t *msg = NULL;
    if (index >= 0 && index < room->message_count) {
        msg = &room->messages[index];
    }

    pthread_rwlock_unlock(&room->lock);
    return msg;
}

/* Get total message count */
int room_get_message_count(chat_room_t *room) {
    pthread_rwlock_rdlock(&room->lock);
    int count = room->message_count;
    pthread_rwlock_unlock(&room->lock);
    return count;
}

/* Get online client count */
int room_get_client_count(chat_room_t *room) {
    pthread_rwlock_rdlock(&room->lock);
    int count = room->client_count;
    pthread_rwlock_unlock(&room->lock);
    return count;
}
