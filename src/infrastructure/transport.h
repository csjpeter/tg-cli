/**
 * @file transport.h
 * @brief TCP transport with MTProto Abridged encoding.
 *
 * Abridged encoding:
 *   - First byte sent: 0xEF (marker)
 *   - Each packet: 1-byte or 3-byte length prefix + payload
 *   - Length < 0x7F: single byte prefix
 *   - Length >= 0x7F: 0x7F + 2-byte LE length
 */

#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int fd;
    int dc_id;
    int connected;
} Transport;

/**
 * @brief Initialize transport (no connection yet).
 */
void transport_init(Transport *t);

/**
 * @brief Connect to a DC.
 * @param host Hostname or IP.
 * @param port Port (default 443).
 * @return 0 on success, -1 on error.
 */
int transport_connect(Transport *t, const char *host, int port);

/**
 * @brief Send a packet with abridged encoding.
 * @param data Payload to send.
 * @param len  Payload length.
 * @return 0 on success, -1 on error.
 */
int transport_send(Transport *t, const uint8_t *data, size_t len);

/**
 * @brief Receive a packet with abridged decoding.
 * @param out     Output buffer.
 * @param max_len Buffer capacity.
 * @param out_len Receives actual payload length.
 * @return 0 on success, -1 on error.
 */
int transport_recv(Transport *t, uint8_t *out, size_t max_len, size_t *out_len);

/**
 * @brief Close transport connection.
 */
void transport_close(Transport *t);

#endif /* TRANSPORT_H */
