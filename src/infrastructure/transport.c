/**
 * @file transport.c
 * @brief TCP transport with MTProto Abridged encoding.
 */

#include "transport.h"
#include "platform/socket.h"

#include <stdlib.h>
#include <string.h>

#define ABRIDGED_MARKER 0xEF

void transport_init(Transport *t) {
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->fd = -1;
}

int transport_connect(Transport *t, const char *host, int port) {
    if (!t || !host) return -1;

    t->fd = sys_socket_create();
    if (t->fd < 0) return -1;

    if (sys_socket_connect(t->fd, host, port) < 0) {
        sys_socket_close(t->fd);
        t->fd = -1;
        return -1;
    }

    /* Send abridged transport marker */
    uint8_t marker = ABRIDGED_MARKER;
    if (sys_socket_send(t->fd, &marker, 1) != 1) {
        sys_socket_close(t->fd);
        t->fd = -1;
        return -1;
    }

    t->connected = 1;
    return 0;
}

int transport_send(Transport *t, const uint8_t *data, size_t len) {
    if (!t || !data || len == 0 || t->fd < 0) return -1;

    /* Abridged: length is in 4-byte units */
    size_t wire_len = len / 4;

    if (wire_len < 0x7F) {
        uint8_t prefix = (uint8_t)wire_len;
        if (sys_socket_send(t->fd, &prefix, 1) != 1) return -1;
    } else {
        uint8_t prefix[4];
        prefix[0] = 0x7F;
        prefix[1] = (uint8_t)(wire_len & 0xFF);
        prefix[2] = (uint8_t)((wire_len >> 8) & 0xFF);
        if (sys_socket_send(t->fd, prefix, 3) != 3) return -1;
    }

    /* Send payload in chunks */
    size_t total = 0;
    while (total < len) {
        ssize_t sent = sys_socket_send(t->fd, data + total, len - total);
        if (sent <= 0) return -1;
        total += (size_t)sent;
    }
    return 0;
}

int transport_recv(Transport *t, uint8_t *out, size_t max_len, size_t *out_len) {
    if (!t || !out || !out_len || t->fd < 0) return -1;

    /* Read first byte of length prefix */
    uint8_t first;
    ssize_t r = sys_socket_recv(t->fd, &first, 1);
    if (r != 1) return -1;

    size_t wire_len;
    if (first < 0x7F) {
        wire_len = first;
    } else {
        uint8_t extra[2];
        r = sys_socket_recv(t->fd, extra, 2);
        if (r != 2) return -1;
        wire_len = (size_t)extra[0] | ((size_t)extra[1] << 8);
    }

    size_t payload_len = wire_len * 4;
    if (payload_len == 0) {
        *out_len = 0;
        return 0;
    }
    if (payload_len > max_len) return -1;

    /* Read payload */
    size_t total = 0;
    while (total < payload_len) {
        r = sys_socket_recv(t->fd, out + total, payload_len - total);
        if (r <= 0) return -1;
        total += (size_t)r;
    }

    *out_len = payload_len;
    return 0;
}

void transport_close(Transport *t) {
    if (t && t->fd >= 0) {
        sys_socket_close(t->fd);
        t->fd = -1;
        t->connected = 0;
    }
}
