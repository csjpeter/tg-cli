/**
 * @file socket.c
 * @brief Mock socket implementation for unit/integration testing.
 *
 * Provides an in-memory fake server that stores sent data and returns
 * pre-programmed responses.  Test accessors in mock_socket.h.
 */

#include "platform/socket.h"

#include <stdlib.h>
#include <string.h>

/* ---- Internal mock state ---- */

#define MOCK_BUF_SIZE (256 * 1024)

static struct {
    int created;
    int connected;
    int closed;
    int nonblocking;

    /* Sent data (client → server) */
    uint8_t *sent;
    size_t   sent_len;
    size_t   sent_cap;

    /* Pre-programmed response (server → client) */
    uint8_t *response;
    size_t   response_len;
    size_t   response_pos;
} g_mock_socket;

/* ---- Test accessor functions ---- */

void mock_socket_reset(void) {
    free(g_mock_socket.sent);
    free(g_mock_socket.response);
    memset(&g_mock_socket, 0, sizeof(g_mock_socket));
}

int mock_socket_was_created(void) { return g_mock_socket.created; }
int mock_socket_was_connected(void) { return g_mock_socket.connected; }
int mock_socket_was_closed(void) { return g_mock_socket.closed; }

const uint8_t *mock_socket_get_sent(size_t *out_len) {
    if (out_len) *out_len = g_mock_socket.sent_len;
    return g_mock_socket.sent;
}

void mock_socket_set_response(const uint8_t *data, size_t len) {
    free(g_mock_socket.response);
    g_mock_socket.response = (uint8_t *)malloc(len);
    memcpy(g_mock_socket.response, data, len);
    g_mock_socket.response_len = len;
    g_mock_socket.response_pos = 0;
}

void mock_socket_append_response(const uint8_t *data, size_t len) {
    if (!data || len == 0) return;
    size_t new_len = g_mock_socket.response_len + len;
    g_mock_socket.response = (uint8_t *)realloc(g_mock_socket.response, new_len);
    memcpy(g_mock_socket.response + g_mock_socket.response_len, data, len);
    g_mock_socket.response_len = new_len;
}

void mock_socket_clear_sent(void) {
    g_mock_socket.sent_len = 0;
}

/* ---- socket.h interface implementation ---- */

int sys_socket_create(void) {
    g_mock_socket.created++;
    return 42; /* fake fd */
}

int sys_socket_connect(int fd, const char *host, int port) {
    (void)fd; (void)host; (void)port;
    g_mock_socket.connected++;
    return 0;
}

ssize_t sys_socket_send(int fd, const void *buf, size_t len) {
    (void)fd;
    if (!buf || len == 0) return 0;

    /* Append to sent buffer */
    if (g_mock_socket.sent_len + len > g_mock_socket.sent_cap) {
        size_t new_cap = g_mock_socket.sent_cap ? g_mock_socket.sent_cap : 4096;
        while (new_cap < g_mock_socket.sent_len + len) new_cap *= 2;
        g_mock_socket.sent = (uint8_t *)realloc(g_mock_socket.sent, new_cap);
        g_mock_socket.sent_cap = new_cap;
    }
    memcpy(g_mock_socket.sent + g_mock_socket.sent_len, buf, len);
    g_mock_socket.sent_len += len;

    return (ssize_t)len;
}

ssize_t sys_socket_recv(int fd, void *buf, size_t len) {
    (void)fd;
    if (!buf || len == 0) return 0;

    size_t avail = g_mock_socket.response_len - g_mock_socket.response_pos;
    if (avail == 0) return 0; /* EOF */

    size_t to_read = len < avail ? len : avail;
    memcpy(buf, g_mock_socket.response + g_mock_socket.response_pos, to_read);
    g_mock_socket.response_pos += to_read;

    return (ssize_t)to_read;
}

int sys_socket_close(int fd) {
    (void)fd;
    g_mock_socket.closed++;
    return 0;
}

int sys_socket_set_nonblocking(int fd) {
    (void)fd;
    g_mock_socket.nonblocking++;
    return 0;
}
