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
/* True when the client holds its name as a signed-in handle, which several
 * sessions may share. */
typedef bool (*room_client_owns_name_fn)(const struct client *client);

struct chat_room;
/* The room a session is in, for a room whose members are listed in another
 * room's client array.  NULL means that room itself. */
typedef const struct chat_room *(*room_client_room_fn)(
    const struct client *client);

/* Runs with the room lock held, so it must not call back into chat_room. */
typedef void (*room_presence_fn)(const char *nickname, const char *room,
                                 bool joined);

/* Fixed-capacity message history.  Entries may wrap physically; callers must
 * use the room message accessors below to observe chronological order. */
typedef struct {
    message_t *entries;
    int start;
    int count;
} message_ring_t;

/* Chat room structure.  The default room's client array lists every
 * interactive session; other rooms keep only history and find their members
 * there through client_room. */
typedef struct chat_room {
    pthread_rwlock_t lock;
    char name[TNT_ROOM_NAME_MAX + 1];
    char log_file[TNT_ROOM_NAME_MAX + 16];
    struct chat_room *sessions;
    struct client **clients;
    int client_count;
    int client_capacity;
    message_ring_t message_history;
    uint64_t update_seq;
    room_client_notifier_fn client_notifier;
    room_client_name_fn client_name;
    room_client_owns_name_fn client_owns_name;
    room_presence_fn presence_notifier;
    room_client_room_fn client_room;
} chat_room_t;

/* Global chat room instance */
extern chat_room_t *g_room;

/* Create the default room, whose history is messages.log. */
chat_room_t* room_create(void);

/* Register `lobby` as the default room and every room-NAME.log in the state
 * directory as a room. */
int room_registry_init(chat_room_t *lobby);
void room_registry_shutdown(void);

/* Find a room by name, creating it when `create` is set.  NULL when the name
 * is invalid, the room does not exist, or TNT_MAX_ROOMS are open.  Rooms live
 * until shutdown, so the pointer stays valid. */
chat_room_t *room_find(const char *name, bool create);

/* Copy up to `capacity` room names, default room first, then by name. */
int room_list(char (*names)[TNT_ROOM_NAME_MAX + 1], int capacity);

/* Destroy chat room */
void room_destroy(chat_room_t *room);

/* Add client to room atomically. Returns 0 on success, -1 when full, and -2
 * when another connected client already owns the same display name, unless
 * the client owns its name. */
int room_add_client(chat_room_t *room, struct client *client);

/* Remove client from room */
void room_remove_client(chat_room_t *room, struct client *client);

/* Broadcast message to the room's members */
void room_broadcast(chat_room_t *room, const message_t *msg);

/* Mark `msg` as belonging to `room`, so message_save() logs it there. */
void room_tag_message(const chat_room_t *room, message_t *msg);

/* Install the non-blocking client notifier used by room_broadcast().  Rooms
 * default to no notifier so chat_room remains independently unit-testable. */
void room_set_client_notifier(chat_room_t *room,
                              room_client_notifier_fn notifier);

/* Install the client display-name accessor used for atomic duplicate checks. */
void room_set_client_name_accessor(chat_room_t *room,
                                   room_client_name_fn accessor);

/* Install the accessor that exempts a client from the duplicate-name check. */
void room_set_client_name_owner(chat_room_t *room,
                                room_client_owns_name_fn accessor);

void room_set_client_room_accessor(chat_room_t *room,
                                   room_client_room_fn accessor);

/* Install the presence hook called when a client joins or leaves. */
void room_set_presence_notifier(chat_room_t *room, room_presence_fn notifier);

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

/* Get online client count: every session for the default room, the room's
 * members for any other */
int room_get_client_count(chat_room_t *room);

/* Get room update sequence */
uint64_t room_get_update_seq(chat_room_t *room);

#endif /* CHAT_ROOM_H */
