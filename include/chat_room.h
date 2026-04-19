#ifndef CHAT_ROOM_H
#define CHAT_ROOM_H

#include "common.h"
#include "message.h"

/* Forward declaration */
struct client;

/* Chat room structure */
typedef struct {
    pthread_rwlock_t lock;
    struct client **clients;
    int client_count;
    int client_capacity;
    message_t *messages;
    int message_count;
    uint64_t update_seq;
} chat_room_t;

/* Global chat room instance */
extern chat_room_t *g_room;

/* Initialize chat room */
chat_room_t* room_create(void);

/* Destroy chat room */
void room_destroy(chat_room_t *room);

/* Add client to room */
int room_add_client(chat_room_t *room, struct client *client);

/* Remove client from room */
void room_remove_client(chat_room_t *room, struct client *client);

/* Broadcast message to all clients */
void room_broadcast(chat_room_t *room, const message_t *msg);

/* Get message by index (thread-safe value copy) */
bool room_get_message(chat_room_t *room, int index, message_t *out);

/* Get total message count */
int room_get_message_count(chat_room_t *room);

/* Get online client count */
int room_get_client_count(chat_room_t *room);

/* Get room update sequence */
uint64_t room_get_update_seq(chat_room_t *room);

#endif /* CHAT_ROOM_H */
