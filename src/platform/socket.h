/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file socket.h
 * @brief Platform-agnostic TCP socket interface (DIP wrapper).
 *
 * Production code links posix/socket.c (or windows/socket.c).
 * Tests link tests/mocks/socket.c (fake server).
 * See ADR-0004 (Dependency Inversion).
 */

#ifndef PLATFORM_SOCKET_H
#define PLATFORM_SOCKET_H

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

/** Create a TCP socket. Returns fd >= 0 on success, -1 on error. */
int     sys_socket_create(void);

/** Connect to host:port. Returns 0 on success, -1 on error. */
int     sys_socket_connect(int fd, const char *host, int port);

/** Send data. Returns bytes sent, or -1 on error. */
ssize_t sys_socket_send(int fd, const void *buf, size_t len);

/** Receive data. Returns bytes received, 0 on EOF, -1 on error. */
ssize_t sys_socket_recv(int fd, void *buf, size_t len);

/** Close socket. Returns 0 on success. */
int     sys_socket_close(int fd);

/** Set non-blocking mode. Returns 0 on success. */
int     sys_socket_set_nonblocking(int fd);

#endif /* PLATFORM_SOCKET_H */
