#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ssh_channel_window_size test_channel_window_size
#define ssh_channel_write test_channel_write
#include "../../src/client.c"
#undef ssh_channel_window_size
#undef ssh_channel_write

static unsigned char *sink;
static size_t sink_capacity;
static size_t sink_len;
static uint32_t channel_window;
static uint32_t write_limit;
static bool write_fails;

uint32_t test_channel_window_size(ssh_channel channel) {
    assert(channel == (ssh_channel)(uintptr_t)1);
    return channel_window;
}

int test_channel_write(ssh_channel channel, const void *data, uint32_t len) {
    uint32_t written = len;

    assert(channel == (ssh_channel)(uintptr_t)1);
    if (write_fails) {
        return SSH_ERROR;
    }
    if (written > channel_window) {
        written = channel_window;
    }
    if (written > write_limit) {
        written = write_limit;
    }
    assert(written > 0);
    assert(sink_len + written <= sink_capacity);
    memcpy(sink + sink_len, data, written);
    sink_len += written;
    channel_window -= written;
    return (int)written;
}

static void reset_transport(unsigned char *buffer, size_t capacity) {
    sink = buffer;
    sink_capacity = capacity;
    sink_len = 0;
    channel_window = 0;
    write_limit = UINT32_MAX;
    write_fails = false;
}

static void init_client(client_t *client) {
    memset(client, 0, sizeof(*client));
    client->channel = (ssh_channel)(uintptr_t)1;
    atomic_init(&client->connected, true);
    atomic_init(&client->pending_bells, 0);
    atomic_init(&client->wake_ready, false);
    atomic_init(&client->wake_pending, false);
    assert(pthread_mutex_init(&client->io_lock, NULL) == 0);
}

static void destroy_client(client_t *client) {
    free(client->outbox);
    assert(pthread_mutex_destroy(&client->io_lock) == 0);
}

static void fill_bytes(unsigned char *data, size_t len, unsigned int seed) {
    size_t i;

    for (i = 0; i < len; i++) {
        data[i] = (unsigned char)((i * 37U + seed) & 0xffU);
    }
}

static void test_wrap_and_partial_writes(void) {
    const size_t first_len = 90000;
    const size_t second_len = 70000;
    const size_t total_len = first_len + second_len;
    unsigned char *first = malloc(first_len);
    unsigned char *second = malloc(second_len);
    unsigned char *expected = malloc(total_len);
    unsigned char *output = malloc(total_len);
    client_t client;

    assert(first && second && expected && output);
    fill_bytes(first, first_len, 11);
    fill_bytes(second, second_len, 29);
    memcpy(expected, first, first_len);
    memcpy(expected + first_len, second, second_len);
    reset_transport(output, total_len);
    init_client(&client);

    assert(client_send(&client, (const char *)first, first_len) == 0);
    assert(client_pending_output(&client) == first_len);

    channel_window = CLIENT_OUTBOX_FLUSH_BUDGET;
    write_limit = 997;
    assert(client_flush_output(&client) == 0);
    assert(sink_len == CLIENT_OUTBOX_FLUSH_BUDGET);
    assert(client.outbox_head == CLIENT_OUTBOX_FLUSH_BUDGET);
    assert(client_pending_output(&client) ==
           first_len - CLIENT_OUTBOX_FLUSH_BUDGET);

    channel_window = 0;
    assert(client_send(&client, (const char *)second, second_len) == 0);
    assert(client_pending_output(&client) ==
           total_len - CLIENT_OUTBOX_FLUSH_BUDGET);

    channel_window = UINT32_MAX;
    write_limit = 4093;
    while (client_pending_output(&client) != 0) {
        assert(client_flush_output(&client) == 0);
    }

    assert(sink_len == total_len);
    assert(memcmp(output, expected, total_len) == 0);
    assert(client.outbox_head == 0);
    assert(atomic_load(&client.connected));

    destroy_client(&client);
    free(output);
    free(expected);
    free(second);
    free(first);
}

static void test_full_queue_disconnects_without_overwrite(void) {
    unsigned char *data = malloc(CLIENT_OUTBOX_CAPACITY);
    unsigned char output[1];
    client_t client;

    assert(data);
    fill_bytes(data, CLIENT_OUTBOX_CAPACITY, 47);
    reset_transport(output, sizeof(output));
    init_client(&client);

    assert(client_send(&client, (const char *)data,
                       CLIENT_OUTBOX_CAPACITY) == 0);
    assert(client_pending_output(&client) == CLIENT_OUTBOX_CAPACITY);
    assert(client_send(&client, "x", 1) == -1);
    assert(!atomic_load(&client.connected));
    assert(client_pending_output(&client) == CLIENT_OUTBOX_CAPACITY);

    destroy_client(&client);
    free(data);
}

static void test_write_error_disconnects(void) {
    unsigned char output[32];
    client_t client;

    reset_transport(output, sizeof(output));
    channel_window = sizeof(output);
    write_fails = true;
    init_client(&client);

    assert(client_send(&client, "write-error", 11) == -1);
    assert(!atomic_load(&client.connected));
    assert(client_pending_output(&client) == 11);
    assert(sink_len == 0);

    destroy_client(&client);
}

int main(void) {
    test_wrap_and_partial_writes();
    test_full_queue_disconnects_without_overwrite();
    test_write_error_disconnects();
    return 0;
}
