/**
 * @file mock_socket.h
 * @brief Test accessor functions for the mock socket implementation.
 */

#ifndef MOCK_SOCKET_H
#define MOCK_SOCKET_H

#include <stddef.h>
#include <stdint.h>

void        mock_socket_reset(void);
int         mock_socket_was_created(void);
int         mock_socket_was_connected(void);
int         mock_socket_was_closed(void);
const uint8_t *mock_socket_get_sent(size_t *out_len);
void        mock_socket_set_response(const uint8_t *data, size_t len);
void        mock_socket_append_response(const uint8_t *data, size_t len);
void        mock_socket_clear_sent(void);

/* Failure injection for error-path coverage.
 * Each "fail_*_at" setter primes the Nth call (1-based) to return -1.
 * Use 0 to disable (default).
 */
void        mock_socket_fail_create(void);       /* next create() fails */
void        mock_socket_fail_connect(void);      /* next connect() fails */
void        mock_socket_fail_send_at(int nth);   /* Nth send() returns -1 */
void        mock_socket_fail_recv_at(int nth);   /* Nth recv() returns -1 */
void        mock_socket_short_send_at(int nth);  /* Nth send() returns 1 short */

#endif /* MOCK_SOCKET_H */
