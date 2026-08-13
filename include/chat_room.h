#ifndef CHAT_ROOM_H
#define CHAT_ROOM_H

#include "common.h"
#include "message.h"

/* Forward declaration */
struct client;

/* Optional, process-local nudge used to wake interactive client loops after a
 * room update.  The callback runs while the room lock protects client
 * lifetime, so it must never block or call back into chat_room APIs. */
typedef void (*room_client_notifier_fn)(struct client *client);
typedef const char *(*room_client_name_fn)(const struct client *client);

/* Fixed-capacity message history.  Entries may wrap physically; callers must
 * use the room message accessors below to observe chronological order. */
typedef struct {
    message_t *entries;
    int start;
    int count;
} message_ring_t;

/* Chat room structure */
typedef struct {
    pthread_rwlock_t lock;
    struct client **clients;
    int client_count;
    int client_capacity;
    message_ring_t message_history;
    uint64_t update_seq;
    room_client_notifier_fn client_notifier;
    room_client_name_fn client_name;
} chat_room_t;

/* Global chat room instance */
extern chat_room_t *g_room;

/* Initialize chat room */
chat_room_t* room_create(void);

/* Destroy chat room */
void room_destroy(chat_room_t *room);

/* Add client to room atomically. Returns 0 on success, -1 when full, and -2
 * when another connected client already owns the same display name. */
int room_add_client(chat_room_t *room, struct client *client);

/* Remove client from room */
void room_remove_client(chat_room_t *room, struct client *client);

/* Broadcast message to all clients */
void room_broadcast(chat_room_t *room, const message_t *msg);

/* Install the non-blocking client notifier used by room_broadcast().  Rooms
 * default to no notifier so chat_room remains independently unit-testable. */
void room_set_client_notifier(chat_room_t *room,
                              room_client_notifier_fn notifier);

/* Install the client display-name accessor used for atomic duplicate checks. */
void room_set_client_name_accessor(chat_room_t *room,
                                   room_client_name_fn accessor);

/* Get message by index (thread-safe value copy) */
bool room_get_message(chat_room_t *room, int index, message_t *out);

/* Copy a chronological message slice beginning at logical index `start`.
 * Returns the number copied, up to `capacity`. */
int room_copy_messages(chat_room_t *room, int start, message_t *out,
                       int capacity);

/* Copy the newest messages in chronological order.  `total_count`, when
 * non-NULL, receives the history size from the same locked snapshot. */
int room_copy_recent_messages(chat_room_t *room, message_t *out, int capacity,
                              int *total_count);

/* Get total message count */
int room_get_message_count(chat_room_t *room);

/* Get online client count */
int room_get_client_count(chat_room_t *room);

/* Get room update sequence */
uint64_t room_get_update_seq(chat_room_t *room);

#endif /* CHAT_ROOM_H */
