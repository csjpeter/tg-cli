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

/**
 * @brief Register a callback that fires after every successful `sys_socket_send`.
 *
 * The mock server emulator uses this hook to parse each frame the client
 * pushes onto the sent buffer, run RPC dispatch, and append an encrypted
 * reply via `mock_socket_append_response` before the next `sys_socket_recv`
 * consumes bytes. The callback receives the full sent buffer + length so it
 * can maintain its own parse cursor; pass NULL to disarm.
 */
typedef void (*MockSocketOnSentFn)(const uint8_t *sent_buf, size_t sent_len);
void        mock_socket_set_on_sent(MockSocketOnSentFn fn);

/* Failure injection for error-path coverage.
 * Each "fail_*_at" setter primes the Nth call (1-based) to return -1.
 * Use 0 to disable (default).
 */
void        mock_socket_fail_create(void);       /* next create() fails */
void        mock_socket_fail_connect(void);      /* next connect() fails */
void        mock_socket_fail_send_at(int nth);   /* Nth send() returns -1 */
void        mock_socket_fail_recv_at(int nth);   /* Nth recv() returns -1 */
void        mock_socket_short_send_at(int nth);  /* Nth send() returns 1 short */
void        mock_socket_eintr_send_at(int nth);  /* Nth send() returns -1/EINTR, next call proceeds normally */
void        mock_socket_eintr_recv_at(int nth);  /* Nth recv() returns -1/EINTR, next call proceeds normally */

/* TEST-82: transport-resilience helpers.
 * These complement the 'fail_*_at' / 'eintr_*_at' knobs above.
 */

/**
 * @brief Cap every `sys_socket_send()` at @p step bytes from the call that
 *        makes this setter onward.  Zero (default) disables the cap.
 *
 * The mock buffers whatever slice it actually accepted and reports that
 * byte count back to the caller.  Production `transport_send()` must loop
 * over the leftover suffix until the full payload is drained.  The
 * `on_sent` emulator hook still fires on every partial write so mock_tel
 * sees each contribution land in order.
 */
void        mock_socket_set_send_fragment(size_t step);

/**
 * @brief Cap every `sys_socket_recv()` at @p step bytes.  Zero disables.
 *
 * Production `transport_recv()` must loop until the announced abridged
 * frame length has been drained; this knob forces it to re-enter the
 * loop multiple times for a single logical frame.
 */
void        mock_socket_set_recv_fragment(size_t step);

/**
 * @brief Prime the NEXT `sys_socket_send()` to return -1 with errno=EINTR.
 *        The call counter is not consulted — this simply catches whichever
 *        send happens first.
 */
void        mock_socket_inject_eintr_next_send(void);

/**
 * @brief Prime the NEXT `sys_socket_recv()` to return -1 with errno=EINTR.
 */
void        mock_socket_inject_eintr_next_recv(void);

/**
 * @brief Prime the NEXT `sys_socket_send()` to return -1 with errno=EAGAIN.
 *        Transport layer is expected to treat EAGAIN equivalently to EINTR.
 */
void        mock_socket_inject_eagain_next_send(void);

/**
 * @brief Prime the NEXT `sys_socket_recv()` to return -1 with errno=EAGAIN.
 */
void        mock_socket_inject_eagain_next_recv(void);

/**
 * @brief Cause the NEXT `sys_socket_recv()` call to return 0 (clean EOF)
 *        regardless of what's queued in the response buffer.  Simulates a
 *        mid-RPC TCP FIN from the peer.  Auto-clears after firing once.
 */
void        mock_socket_kill_on_next_recv(void);

/**
 * @brief Short-circuit `sys_socket_connect()` to return -1 with
 *        errno=ECONNREFUSED until reset.
 *        Different from `mock_socket_fail_connect()` — the latter is a
 *        one-shot, this one persists so every reconnect attempt in a
 *        retry loop also fails.
 */
void        mock_socket_refuse_connect(void);

#endif /* MOCK_SOCKET_H */
