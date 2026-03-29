/**
 * @file mtproto_rpc.h
 * @brief MTProto RPC framework — send/receive encrypted and unencrypted messages.
 *
 * Unencrypted wire format (used during DH key exchange):
 *   [auth_key_id = 0: 8 bytes] [msg_id: 8 bytes] [len: 4 bytes] [data: N bytes]
 *
 * Encrypted wire format (after auth_key established):
 *   [auth_key_id: 8 bytes] [msg_key: 16 bytes] [encrypted(payload): N bytes]
 *   where payload = [salt:8][session_id:8][msg_id:8][seq_no:4][len:4][data:N][padding]
 */

#ifndef MTPROTO_RPC_H
#define MTPROTO_RPC_H

#include "mtproto_session.h"
#include "transport.h"

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Send an unencrypted message (used during DH key exchange).
 * @param s  Session (for msg_id generation).
 * @param t  Transport.
 * @param data TL-encoded payload.
 * @param len  Payload length.
 * @return 0 on success, -1 on error.
 */
int rpc_send_unencrypted(MtProtoSession *s, Transport *t,
                         const uint8_t *data, size_t len);

/**
 * @brief Receive an unencrypted message (used during DH key exchange).
 * @param s       Session.
 * @param t       Transport.
 * @param out     Output buffer for payload.
 * @param max_len Buffer capacity.
 * @param out_len Receives payload length.
 * @return 0 on success, -1 on error.
 */
int rpc_recv_unencrypted(MtProtoSession *s, Transport *t,
                         uint8_t *out, size_t max_len, size_t *out_len);

/**
 * @brief Send an encrypted message (after auth_key established).
 * @param s  Session (must have auth_key).
 * @param t  Transport.
 * @param data TL-encoded payload.
 * @param len  Payload length.
 * @param content_related  1 for RPC calls, 0 for acks/pings.
 * @return 0 on success, -1 on error.
 */
int rpc_send_encrypted(MtProtoSession *s, Transport *t,
                       const uint8_t *data, size_t len,
                       int content_related);

/**
 * @brief Receive an encrypted message.
 * @param s       Session (must have auth_key).
 * @param t       Transport.
 * @param out     Output buffer for decrypted payload.
 * @param max_len Buffer capacity.
 * @param out_len Receives payload length.
 * @return 0 on success, -1 on error.
 */
int rpc_recv_encrypted(MtProtoSession *s, Transport *t,
                       uint8_t *out, size_t max_len, size_t *out_len);

#endif /* MTPROTO_RPC_H */
