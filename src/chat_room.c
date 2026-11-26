#include "chat_room.h"
#include "config_defaults.h"
#include <dirent.h>
#include <strings.h>

/* Global chat room instance */
chat_room_t *g_room = NULL;

static pthread_mutex_t g_rooms_lock = PTHREAD_MUTEX_INITIALIZER;
static chat_room_t *g_rooms[TNT_MAX_ROOMS];
static int g_room_count = 0;

static int room_capacity_from_env(void) {
    return tnt_config_env_int(&TNT_CONFIG_MAX_CONNECTIONS);
}

static chat_room_t *room_alloc(const char *name, chat_room_t *sessions) {
    chat_room_t *room = calloc(1, sizeof(chat_room_t));
    if (!room) return NULL;

    snprintf(room->name, sizeof(room->name), "%s", name);
    message_log_file_for(name, room->log_file, sizeof(room->log_file));
    room->sessions = sessions;

    if (pthread_rwlock_init(&room->lock, NULL) != 0) {
        free(room);
        return NULL;
    }
    room->client_capacity = sessions ? 0 : room_capacity_from_env();
    if (!sessions) {
        room->clients = calloc(room->client_capacity, sizeof(struct client *));
        if (!room->clients) {
            pthread_rwlock_destroy(&room->lock);
            free(room);
            return NULL;
        }
    }

    room->message_history.count = message_load_from(
        room->log_file, &room->message_history.entries, MAX_MESSAGES);
    if (!room->message_history.entries) {
        free(room->clients);
        pthread_rwlock_destroy(&room->lock);
        free(room);
        return NULL;
    }
    for (int i = 0; i < room->message_history.count; i++) {
        room_tag_message(room, &room->message_history.entries[i]);
        message_prepare_display(&room->message_history.entries[i]);
    }

    return room;
}

chat_room_t* room_create(void) {
    return room_alloc(TNT_DEFAULT_ROOM, NULL);
}

static chat_room_t *room_create_named(const char *name,
                                      chat_room_t *sessions) {
    if (!tnt_room_name_is_valid(name) || !sessions ||
        message_room_is_default(name)) {
        return NULL;
    }
    return room_alloc(name, sessions);
}

static chat_room_t *room_register_locked(const char *name) {
    chat_room_t *room;

    if (g_room_count == 0 || g_room_count >= TNT_MAX_ROOMS) {
        return NULL;
    }
    room = room_create_named(name, g_rooms[0]);
    if (room) {
        g_rooms[g_room_count++] = room;
    }
    return room;
}

int room_registry_init(chat_room_t *lobby) {
    DIR *dir;
    struct dirent *entry;

    if (!lobby) return -1;

    pthread_mutex_lock(&g_rooms_lock);
    g_rooms[0] = lobby;
    g_room_count = 1;
    dir = opendir(tnt_state_dir());
    while (dir && (entry = readdir(dir)) != NULL) {
        const char *file = entry->d_name;
        size_t len = strlen(file);
        char name[TNT_ROOM_NAME_MAX + 1];

        if (len <= 9 || len - 9 > TNT_ROOM_NAME_MAX ||
            strncmp(file, "room-", 5) != 0 ||
            strcmp(file + len - 4, ".log") != 0) {
            continue;
        }
        snprintf(name, sizeof(name), "%.*s", (int)(len - 9), file + 5);
        if (tnt_room_name_is_valid(name) &&
            strcmp(name, TNT_DEFAULT_ROOM) != 0 &&
            !room_register_locked(name) && g_room_count >= TNT_MAX_ROOMS) {
            fprintf(stderr, "rooms: more than %d rooms, %s not loaded\n",
                    TNT_MAX_ROOMS, file);
        }
    }
    if (dir) closedir(dir);
    pthread_mutex_unlock(&g_rooms_lock);
    return 0;
}

void room_registry_shutdown(void) {
    pthread_mutex_lock(&g_rooms_lock);
    for (int i = 1; i < g_room_count; i++) {
        room_destroy(g_rooms[i]);
        g_rooms[i] = NULL;
    }
    g_rooms[0] = NULL;
    g_room_count = 0;
    pthread_mutex_unlock(&g_rooms_lock);
}

chat_room_t *room_find(const char *name, bool create) {
    chat_room_t *room = NULL;

    if (!tnt_room_name_is_valid(name)) return NULL;

    pthread_mutex_lock(&g_rooms_lock);
    for (int i = 0; i < g_room_count && !room; i++) {
        if (strcmp(g_rooms[i]->name, name) == 0) {
            room = g_rooms[i];
        }
    }
    if (!room && create) {
        room = room_register_locked(name);
    }
    pthread_mutex_unlock(&g_rooms_lock);
    return room;
}

static int compare_room_names(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

int room_list(char (*names)[TNT_ROOM_NAME_MAX + 1], int capacity) {
    int count = 0;

    if (!names || capacity <= 0) return 0;

    pthread_mutex_lock(&g_rooms_lock);
    for (int i = 0; i < g_room_count && count < capacity; i++) {
        snprintf(names[count++], TNT_ROOM_NAME_MAX + 1, "%s", g_rooms[i]->name);
    }
    pthread_mutex_unlock(&g_rooms_lock);
    if (count > 2) {
        qsort(names + 1, (size_t)count - 1, sizeof(*names), compare_room_names);
    }
    return count;
}

static const chat_room_t *room_of_locked(const chat_room_t *room,
                                         const struct client *client) {
    const chat_room_t *in = room->client_room ? room->client_room(client) : NULL;
    return in ? in : room;
}

/* Destroy chat room */
void room_destroy(chat_room_t *room) {
    if (!room) return;

    pthread_rwlock_wrlock(&room->lock);

    free(room->clients);
    free(room->message_history.entries);

    pthread_rwlock_unlock(&room->lock);
    pthread_rwlock_destroy(&room->lock);

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

    if (room->client_name &&
        !(room->client_owns_name && room->client_owns_name(client))) {
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
    if (room->presence_notifier && room->client_name) {
        room->presence_notifier(room->client_name(client), room->name, true);
    }

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
            if (room->presence_notifier && room->client_name) {
                room->presence_notifier(room->client_name(client),
                                        room->name, false);
            }
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
    room_tag_message(room, &history->entries[index]);
    message_prepare_display(&history->entries[index]);
}

/* The sessions lock prevents a session from removing and releasing its final
 * reference while it is being nudged.  Production installs a non-blocking
 * directed-signal notifier; unit tests may inject an equally non-blocking
 * spy. */
static void room_notify_members_locked(chat_room_t *sessions,
                                       const chat_room_t *target) {
    if (!sessions->client_notifier) return;

    for (int i = 0; i < sessions->client_count; i++) {
        if (room_of_locked(sessions, sessions->clients[i]) == target) {
            sessions->client_notifier(sessions->clients[i]);
        }
    }
}

void room_tag_message(const chat_room_t *room, message_t *msg) {
    if (room && msg) {
        snprintf(msg->room, sizeof(msg->room), "%s",
                 room->sessions ? room->name : "");
    }
}

/* Broadcast message to the room's members */
void room_broadcast(chat_room_t *room, const message_t *msg) {
    if (!room || !msg) return;

    pthread_rwlock_wrlock(&room->lock);

    room_add_message(room, msg);
    room->update_seq++;

    if (!room->sessions) {
        room_notify_members_locked(room, room);
        pthread_rwlock_unlock(&room->lock);
        return;
    }
    pthread_rwlock_unlock(&room->lock);

    pthread_rwlock_rdlock(&room->sessions->lock);
    room_notify_members_locked(room->sessions, room);
    pthread_rwlock_unlock(&room->sessions->lock);
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

void room_set_client_name_owner(chat_room_t *room,
                                room_client_owns_name_fn accessor) {
    if (!room) return;

    pthread_rwlock_wrlock(&room->lock);
    room->client_owns_name = accessor;
    pthread_rwlock_unlock(&room->lock);
}

void room_set_client_room_accessor(chat_room_t *room,
                                   room_client_room_fn accessor) {
    if (!room) return;

    pthread_rwlock_wrlock(&room->lock);
    room->client_room = accessor;
    pthread_rwlock_unlock(&room->lock);
}

void room_set_presence_notifier(chat_room_t *room, room_presence_fn notifier) {
    if (!room) return;

    pthread_rwlock_wrlock(&room->lock);
    room->presence_notifier = notifier;
    pthread_rwlock_unlock(&room->lock);
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

/* The default room counts every session; another room counts its members */
int room_get_client_count(chat_room_t *room) {
    chat_room_t *sessions;
    int count = 0;

    if (!room) return 0;

    sessions = room->sessions ? room->sessions : room;
    pthread_rwlock_rdlock(&sessions->lock);
    for (int i = 0; i < sessions->client_count; i++) {
        if (!room->sessions ||
            room_of_locked(sessions, sessions->clients[i]) == room) {
            count++;
        }
    }
    pthread_rwlock_unlock(&sessions->lock);
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
