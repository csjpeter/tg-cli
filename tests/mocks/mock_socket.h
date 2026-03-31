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

#endif /* MOCK_SOCKET_H */
