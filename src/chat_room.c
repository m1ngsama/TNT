#include "chat_room.h"

/* Global chat room instance */
chat_room_t *g_room = NULL;

static int room_capacity_from_env(void) {
    const char *env = getenv("TNT_MAX_CONNECTIONS");

    if (!env || env[0] == '\0') {
        return MAX_CLIENTS;
    }

    char *end;
    long capacity = strtol(env, &end, 10);
    if (*end != '\0' || capacity < 1 || capacity > 1024) {
        return MAX_CLIENTS;
    }

    return (int)capacity;
}

/* Initialize chat room */
chat_room_t* room_create(void) {
    chat_room_t *room = calloc(1, sizeof(chat_room_t));
    if (!room) return NULL;

    pthread_rwlock_init(&room->lock, NULL);

    room->client_capacity = room_capacity_from_env();
    room->clients = calloc(room->client_capacity, sizeof(struct client *));
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
int room_add_client(chat_room_t *room, struct client *client) {
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
void room_remove_client(chat_room_t *room, struct client *client) {
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

/* Add message to room history (caller must hold write lock) */
static void room_add_message(chat_room_t *room, const message_t *msg) {
    if (room->message_count >= MAX_MESSAGES) {
        memmove(&room->messages[0], &room->messages[1],
                (MAX_MESSAGES - 1) * sizeof(message_t));
        room->message_count = MAX_MESSAGES - 1;
    }

    room->messages[room->message_count++] = *msg;
}

/* Broadcast message to all clients */
void room_broadcast(chat_room_t *room, const message_t *msg) {
    pthread_rwlock_wrlock(&room->lock);

    room_add_message(room, msg);
    room->update_seq++;

    pthread_rwlock_unlock(&room->lock);
}

/* Get message by index (thread-safe value copy) */
bool room_get_message(chat_room_t *room, int index, message_t *out) {
    if (!room || !out) return false;

    pthread_rwlock_rdlock(&room->lock);

    bool found = false;
    if (index >= 0 && index < room->message_count) {
        *out = room->messages[index];
        found = true;
    }

    pthread_rwlock_unlock(&room->lock);
    return found;
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

uint64_t room_get_update_seq(chat_room_t *room) {
    uint64_t seq;

    pthread_rwlock_rdlock(&room->lock);
    seq = room->update_seq;
    pthread_rwlock_unlock(&room->lock);

    return seq;
}
