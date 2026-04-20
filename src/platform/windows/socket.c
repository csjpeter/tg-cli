/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file socket.c
 * @brief Winsock2 TCP socket implementation for Windows (MinGW-w64).
 *
 * SOCKET handles are cast to/from int. On 64-bit Windows, SOCKET is UINT_PTR
 * (64-bit), so the upper 32 bits are lost when stored in int. In practice
 * Winsock2 socket values fit in 32 bits for typical workloads, but callers
 * that need full 64-bit handle support should switch the interface to intptr_t.
 */

#include "../socket.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>

/* WSAStartup is called once on first socket creation. */
static int wsa_initialized = 0;

static int wsa_init(void) {
    if (wsa_initialized) return 0;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
    wsa_initialized = 1;
    return 0;
}

int sys_socket_create(void) {
    if (wsa_init() != 0) return -1;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return -1;

    /* Disable Nagle algorithm */
    BOOL flag = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&flag, sizeof(flag));

    return (int)(intptr_t)s;
}

int sys_socket_connect(int fd, const char *host, int port) {
    SOCKET s = (SOCKET)(intptr_t)fd;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0) return -1;

    int rc = (connect(s, res->ai_addr, (int)res->ai_addrlen) == 0) ? 0 : -1;
    freeaddrinfo(res);
    return rc;
}

ssize_t sys_socket_send(int fd, const void *buf, size_t len) {
    SOCKET s = (SOCKET)(intptr_t)fd;
    int sent = send(s, (const char *)buf, (int)len, 0);
    return (sent == SOCKET_ERROR) ? -1 : (ssize_t)sent;
}

ssize_t sys_socket_recv(int fd, void *buf, size_t len) {
    SOCKET s = (SOCKET)(intptr_t)fd;
    int received = recv(s, (char *)buf, (int)len, 0);
    if (received == SOCKET_ERROR) return -1;
    return (ssize_t)received;
}

int sys_socket_close(int fd) {
    SOCKET s = (SOCKET)(intptr_t)fd;
    int rc = (closesocket(s) == 0) ? 0 : -1;
    if (wsa_initialized) {
        WSACleanup();
        wsa_initialized = 0;
    }
    return rc;
}

int sys_socket_set_nonblocking(int fd) {
    SOCKET s = (SOCKET)(intptr_t)fd;
    u_long mode = 1;
    return (ioctlsocket(s, FIONBIO, &mode) == 0) ? 0 : -1;
}
