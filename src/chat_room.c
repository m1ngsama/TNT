#include "chat_room.h"
#include "config_defaults.h"
#include <strings.h>

/* Global chat room instance */
chat_room_t *g_room = NULL;

static int room_capacity_from_env(void) {
    return tnt_config_env_int(&TNT_CONFIG_MAX_CONNECTIONS);
}

static uint64_t room_monotonic_ns(void) {
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}

/* Initialize chat room */
chat_room_t* room_create(void) {
    chat_room_t *room = calloc(1, sizeof(chat_room_t));
    if (!room) return NULL;

    if (pthread_rwlock_init(&room->lock, NULL) != 0) {
        free(room);
        return NULL;
    }
    if (pthread_mutex_init(&room->distribution_lock, NULL) != 0) {
        pthread_rwlock_destroy(&room->lock);
        free(room);
        return NULL;
    }

    room->client_capacity = room_capacity_from_env();
    room->clients = calloc(room->client_capacity, sizeof(struct client *));
    if (!room->clients) {
        pthread_mutex_destroy(&room->distribution_lock);
        pthread_rwlock_destroy(&room->lock);
        free(room);
        return NULL;
    }

    /* Load messages from file */
    room->message_history.count = message_load(&room->message_history.entries,
                                               MAX_MESSAGES);
    if (!room->message_history.entries) {
        free(room->clients);
        pthread_mutex_destroy(&room->distribution_lock);
        pthread_rwlock_destroy(&room->lock);
        free(room);
        return NULL;
    }
    for (int i = 0; i < room->message_history.count; i++) {
        message_prepare_display(&room->message_history.entries[i]);
    }

    return room;
}

/* Destroy chat room */
void room_destroy(chat_room_t *room) {
    if (!room) return;

    pthread_rwlock_wrlock(&room->lock);

    free(room->clients);
    free(room->message_history.entries);

    pthread_rwlock_unlock(&room->lock);
    pthread_rwlock_destroy(&room->lock);
    pthread_mutex_destroy(&room->distribution_lock);

    free(room);
}

/* Add client to room */
int room_add_client(chat_room_t *room, struct client *client) {
    if (!room || !client) return -1;

    pthread_rwlock_wrlock(&room->lock);

    if (room->client_count >= room->client_capacity) {
        pthread_rwlock_unlock(&room->lock);
        return -1;
    }

    if (room->client_name) {
        const char *candidate_name = room->client_name(client);
        for (int i = 0; i < room->client_count; i++) {
            const char *existing_name = room->client_name(room->clients[i]);
            if (candidate_name && existing_name &&
                strcasecmp(existing_name, candidate_name) == 0) {
                pthread_rwlock_unlock(&room->lock);
                return -2;
            }
        }
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
    message_ring_t *history = &room->message_history;
    int index;

    if (history->count < MAX_MESSAGES) {
        index = (history->start + history->count) % MAX_MESSAGES;
        history->count++;
    } else {
        index = history->start;
        history->start = (history->start + 1) % MAX_MESSAGES;
    }

    history->entries[index] = *msg;
    message_prepare_display(&history->entries[index]);
}

/* Broadcast message to all clients */
void room_broadcast(chat_room_t *room, const message_t *msg) {
    if (!room || !msg) return;

    pthread_rwlock_wrlock(&room->lock);

    room_add_message(room, msg);
    room->update_seq++;

    pthread_mutex_lock(&room->distribution_lock);
    room->distribution_started_ns = room_monotonic_ns();
    room->distribution_seq = room->update_seq;
    room->distribution_expected = room->client_count;
    room->distribution_completed = 0;
    if (room->distribution_expected == 0) {
        room->distribution_last_complete_seq = room->distribution_seq;
        room->distribution_last_complete_latency_us = 0;
    }
    pthread_mutex_unlock(&room->distribution_lock);

    /* The room lock prevents a session from removing and releasing its final
     * reference while it is being nudged.  Production installs a non-blocking
     * directed-signal notifier; unit tests may inject an equally non-blocking
     * spy. */
    if (room->client_notifier) {
        for (int i = 0; i < room->client_count; i++) {
            room->client_notifier(room->clients[i], room->update_seq);
        }
    }

    pthread_rwlock_unlock(&room->lock);
}

void room_set_client_notifier(chat_room_t *room,
                              room_client_notifier_fn notifier) {
    if (!room) return;

    pthread_rwlock_wrlock(&room->lock);
    room->client_notifier = notifier;
    pthread_rwlock_unlock(&room->lock);
}

void room_set_client_name_accessor(chat_room_t *room,
                                   room_client_name_fn accessor) {
    if (!room) return;

    pthread_rwlock_wrlock(&room->lock);
    room->client_name = accessor;
    pthread_rwlock_unlock(&room->lock);
}

void room_set_client_render_ack(chat_room_t *room,
                                room_client_render_ack_fn ack) {
    if (!room) return;

    pthread_rwlock_wrlock(&room->lock);
    room->client_render_ack = ack;
    pthread_rwlock_unlock(&room->lock);
}

void room_record_client_rendered(chat_room_t *room, struct client *client,
                                 uint64_t seq) {
    room_client_render_ack_fn acknowledge;
    uint64_t completed_ns;

    if (!room || !client || seq == 0) return;

    pthread_rwlock_rdlock(&room->lock);
    acknowledge = room->client_render_ack;
    pthread_rwlock_unlock(&room->lock);
    if (!acknowledge || !acknowledge(client, seq)) return;

    completed_ns = room_monotonic_ns();
    pthread_mutex_lock(&room->distribution_lock);
    if (room->distribution_seq == seq &&
        room->distribution_completed < room->distribution_expected) {
        room->distribution_completed++;
        if (room->distribution_completed == room->distribution_expected) {
            room->distribution_last_complete_seq = seq;
            if (room->distribution_started_ns != 0 &&
                completed_ns >= room->distribution_started_ns) {
                room->distribution_last_complete_latency_us =
                    (completed_ns - room->distribution_started_ns) / 1000ULL;
            } else {
                room->distribution_last_complete_latency_us = 0;
            }
        }
    }
    pthread_mutex_unlock(&room->distribution_lock);
}

void room_get_distribution_stats(chat_room_t *room,
                                 room_distribution_stats_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!room) return;

    pthread_mutex_lock(&room->distribution_lock);
    out->current_seq = room->distribution_seq;
    out->expected_clients = room->distribution_expected;
    out->completed_clients = room->distribution_completed;
    out->last_complete_seq = room->distribution_last_complete_seq;
    out->last_complete_latency_us =
        room->distribution_last_complete_latency_us;
    pthread_mutex_unlock(&room->distribution_lock);
}

/* Get message by index (thread-safe value copy) */
bool room_get_message(chat_room_t *room, int index, message_t *out) {
    if (!room || !out) return false;

    pthread_rwlock_rdlock(&room->lock);

    bool found = false;
    if (index >= 0 && index < room->message_history.count) {
        int physical = (room->message_history.start + index) % MAX_MESSAGES;
        *out = room->message_history.entries[physical];
        found = true;
    }

    pthread_rwlock_unlock(&room->lock);
    return found;
}

static int room_copy_messages_locked(const chat_room_t *room, int start,
                                     message_t *out, int capacity) {
    const message_ring_t *history = &room->message_history;
    int copy_count;
    int physical;
    int first_count;

    if (!out || capacity <= 0 || start < 0 || start >= history->count) {
        return 0;
    }

    copy_count = history->count - start;
    if (copy_count > capacity) {
        copy_count = capacity;
    }

    physical = (history->start + start) % MAX_MESSAGES;
    first_count = MAX_MESSAGES - physical;
    if (first_count > copy_count) {
        first_count = copy_count;
    }

    memcpy(out, &history->entries[physical],
           (size_t)first_count * sizeof(*out));
    if (first_count < copy_count) {
        memcpy(out + first_count, history->entries,
               (size_t)(copy_count - first_count) * sizeof(*out));
    }

    return copy_count;
}

int room_copy_messages(chat_room_t *room, int start, message_t *out,
                       int capacity) {
    int copied;

    if (!room || !out || capacity <= 0 || start < 0) {
        return 0;
    }

    pthread_rwlock_rdlock(&room->lock);
    copied = room_copy_messages_locked(room, start, out, capacity);
    pthread_rwlock_unlock(&room->lock);

    return copied;
}

int room_copy_recent_messages(chat_room_t *room, message_t *out, int capacity,
                              int *total_count) {
    int count;
    int start;
    int copied;

    if (total_count) {
        *total_count = 0;
    }
    if (!room || !out || capacity <= 0) {
        return 0;
    }

    pthread_rwlock_rdlock(&room->lock);
    count = room->message_history.count;
    start = count > capacity ? count - capacity : 0;
    copied = room_copy_messages_locked(room, start, out, capacity);
    if (total_count) {
        *total_count = count;
    }
    pthread_rwlock_unlock(&room->lock);

    return copied;
}

/* Get total message count */
int room_get_message_count(chat_room_t *room) {
    if (!room) return 0;

    pthread_rwlock_rdlock(&room->lock);
    int count = room->message_history.count;
    pthread_rwlock_unlock(&room->lock);
    return count;
}

/* Get online client count */
int room_get_client_count(chat_room_t *room) {
    if (!room) return 0;

    pthread_rwlock_rdlock(&room->lock);
    int count = room->client_count;
    pthread_rwlock_unlock(&room->lock);
    return count;
}

uint64_t room_get_update_seq(chat_room_t *room) {
    uint64_t seq;

    if (!room) return 0;

    pthread_rwlock_rdlock(&room->lock);
    seq = room->update_seq;
    pthread_rwlock_unlock(&room->lock);

    return seq;
}
