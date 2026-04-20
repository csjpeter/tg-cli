/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file socket.c
 * @brief POSIX TCP socket implementation.
 */

#include "../socket.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

int sys_socket_create(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    return fd;
}

int sys_socket_connect(int fd, const char *host, int port) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    int rc = getaddrinfo(host, port_str, &hints, &res);
    if (rc != 0) return -1;

    rc = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    return rc;
}

ssize_t sys_socket_send(int fd, const void *buf, size_t len) {
    return send(fd, buf, len, 0);
}

ssize_t sys_socket_recv(int fd, void *buf, size_t len) {
    return recv(fd, buf, len, 0);
}

int sys_socket_close(int fd) {
    return close(fd);
}

int sys_socket_set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
