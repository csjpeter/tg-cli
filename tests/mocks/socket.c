/**
 * @file socket.c
 * @brief Mock socket implementation for unit/integration testing.
 *
 * Provides an in-memory fake server that stores sent data and returns
 * pre-programmed responses.  Test accessors in mock_socket.h.
 */

#include "platform/socket.h"

#include <errno.h>
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

    /* Failure injection */
    int fail_create;
    int fail_connect;
    int refuse_connect;  /* persistent ECONNREFUSED */
    int fail_send_at;    /* 0 = never */
    int fail_recv_at;
    int short_send_at;
    int eintr_send_at;   /* Nth send() returns -1/EINTR, then succeeds */
    int eintr_recv_at;   /* Nth recv() returns -1/EINTR, then succeeds */
    int send_call_n;     /* call counters */
    int recv_call_n;

    /* TEST-82: fragmentation + one-shot EINTR/EAGAIN/EOF */
    size_t send_fragment;         /* cap each send() at N bytes; 0 = off */
    size_t recv_fragment;         /* cap each recv() at N bytes; 0 = off */
    int    inject_eintr_send;     /* next send() returns -1/EINTR */
    int    inject_eintr_recv;     /* next recv() returns -1/EINTR */
    int    inject_eagain_send;    /* next send() returns -1/EAGAIN */
    int    inject_eagain_recv;    /* next recv() returns -1/EAGAIN */
    int    kill_next_recv;        /* next recv() returns 0 (EOF) */

    /* Emulator hook: runs after every successful send, lets the fake
     * server parse + reply before the next recv. */
    void (*on_sent)(const uint8_t *, size_t);
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

void mock_socket_fail_create(void)      { g_mock_socket.fail_create  = 1; }
void mock_socket_fail_connect(void)     { g_mock_socket.fail_connect = 1; }
void mock_socket_fail_send_at(int n)    { g_mock_socket.fail_send_at = n; }
void mock_socket_fail_recv_at(int n)    { g_mock_socket.fail_recv_at = n; }
void mock_socket_short_send_at(int n)   { g_mock_socket.short_send_at = n; }
void mock_socket_eintr_send_at(int n)   { g_mock_socket.eintr_send_at = n; }
void mock_socket_eintr_recv_at(int n)   { g_mock_socket.eintr_recv_at = n; }

void mock_socket_set_send_fragment(size_t step) { g_mock_socket.send_fragment = step; }
void mock_socket_set_recv_fragment(size_t step) { g_mock_socket.recv_fragment = step; }
void mock_socket_inject_eintr_next_send(void)   { g_mock_socket.inject_eintr_send  = 1; }
void mock_socket_inject_eintr_next_recv(void)   { g_mock_socket.inject_eintr_recv  = 1; }
void mock_socket_inject_eagain_next_send(void)  { g_mock_socket.inject_eagain_send = 1; }
void mock_socket_inject_eagain_next_recv(void)  { g_mock_socket.inject_eagain_recv = 1; }
void mock_socket_kill_on_next_recv(void)        { g_mock_socket.kill_next_recv     = 1; }
void mock_socket_refuse_connect(void)           { g_mock_socket.refuse_connect     = 1; }

void mock_socket_set_on_sent(void (*fn)(const uint8_t *, size_t)) {
    g_mock_socket.on_sent = fn;
}

/* ---- socket.h interface implementation ---- */

int sys_socket_create(void) {
    if (g_mock_socket.fail_create) {
        g_mock_socket.fail_create = 0;
        return -1;
    }
    g_mock_socket.created++;
    return 42; /* fake fd */
}

int sys_socket_connect(int fd, const char *host, int port) {
    (void)fd; (void)host; (void)port;
    if (g_mock_socket.refuse_connect) {
        /* Persistent refusal — every reconnect attempt fails with
         * ECONNREFUSED until mock_socket_reset() clears it. */
        errno = ECONNREFUSED;
        return -1;
    }
    if (g_mock_socket.fail_connect) {
        g_mock_socket.fail_connect = 0;
        return -1;
    }
    g_mock_socket.connected++;
    return 0;
}

ssize_t sys_socket_send(int fd, const void *buf, size_t len) {
    (void)fd;
    if (!buf || len == 0) return 0;

    g_mock_socket.send_call_n++;

    /* Next-call EINTR / EAGAIN injection beats the Nth-call variant —
     * tests that do not want to count calls can use these one-shots. */
    if (g_mock_socket.inject_eintr_send) {
        g_mock_socket.inject_eintr_send = 0;
        errno = EINTR;
        return -1;
    }
    if (g_mock_socket.inject_eagain_send) {
        g_mock_socket.inject_eagain_send = 0;
        errno = EAGAIN;
        return -1;
    }

    if (g_mock_socket.eintr_send_at &&
        g_mock_socket.send_call_n == g_mock_socket.eintr_send_at) {
        g_mock_socket.eintr_send_at = 0;
        errno = EINTR;
        return -1;
    }

    if (g_mock_socket.fail_send_at &&
        g_mock_socket.send_call_n == g_mock_socket.fail_send_at) {
        g_mock_socket.fail_send_at = 0;
        return -1;
    }

    /* send_fragment: emit at most `step` bytes per call.  Distinct from
     * short_send_at which is one-shot — send_fragment persists so the
     * transport layer loops until the caller's payload is drained. */
    size_t emit = len;
    if (g_mock_socket.send_fragment && emit > g_mock_socket.send_fragment) {
        emit = g_mock_socket.send_fragment;
    }

    /* Append to sent buffer */
    if (g_mock_socket.sent_len + emit > g_mock_socket.sent_cap) {
        size_t new_cap = g_mock_socket.sent_cap ? g_mock_socket.sent_cap : 4096;
        while (new_cap < g_mock_socket.sent_len + emit) new_cap *= 2;
        g_mock_socket.sent = (uint8_t *)realloc(g_mock_socket.sent, new_cap);
        g_mock_socket.sent_cap = new_cap;
    }
    memcpy(g_mock_socket.sent + g_mock_socket.sent_len, buf, emit);
    g_mock_socket.sent_len += emit;

    if (g_mock_socket.short_send_at &&
        g_mock_socket.send_call_n == g_mock_socket.short_send_at && emit > 1) {
        g_mock_socket.short_send_at = 0;
        /* Hook fires after short send too so the server can react to
         * the partial frame exactly as the real DC would. */
        if (g_mock_socket.on_sent) {
            g_mock_socket.on_sent(g_mock_socket.sent, g_mock_socket.sent_len);
        }
        return (ssize_t)(emit - 1);
    }

    if (g_mock_socket.on_sent) {
        g_mock_socket.on_sent(g_mock_socket.sent, g_mock_socket.sent_len);
    }
    return (ssize_t)emit;
}

ssize_t sys_socket_recv(int fd, void *buf, size_t len) {
    (void)fd;
    if (!buf || len == 0) return 0;

    g_mock_socket.recv_call_n++;

    /* Kill-on-next wins unconditionally so mid-RPC disconnect tests can
     * interleave it with queued responses.  We also flush any pending
     * response bytes: a real peer that closed the socket before the
     * reply was written never delivers those bytes, so the client must
     * reconnect and re-issue the RPC rather than finding the old reply
     * on the next recv. */
    if (g_mock_socket.kill_next_recv) {
        g_mock_socket.kill_next_recv = 0;
        g_mock_socket.response_pos = g_mock_socket.response_len;
        return 0;
    }

    if (g_mock_socket.inject_eintr_recv) {
        g_mock_socket.inject_eintr_recv = 0;
        errno = EINTR;
        return -1;
    }
    if (g_mock_socket.inject_eagain_recv) {
        g_mock_socket.inject_eagain_recv = 0;
        errno = EAGAIN;
        return -1;
    }

    if (g_mock_socket.eintr_recv_at &&
        g_mock_socket.recv_call_n == g_mock_socket.eintr_recv_at) {
        g_mock_socket.eintr_recv_at = 0;
        errno = EINTR;
        return -1;
    }

    if (g_mock_socket.fail_recv_at &&
        g_mock_socket.recv_call_n == g_mock_socket.fail_recv_at) {
        g_mock_socket.fail_recv_at = 0;
        return -1;
    }

    size_t avail = g_mock_socket.response_len - g_mock_socket.response_pos;
    if (avail == 0) return 0; /* EOF */

    size_t to_read = len < avail ? len : avail;
    if (g_mock_socket.recv_fragment && to_read > g_mock_socket.recv_fragment) {
        to_read = g_mock_socket.recv_fragment;
    }
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
