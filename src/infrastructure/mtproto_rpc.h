/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

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

/**
 * @brief Unwrap gzip_packed TL object if present.
 *
 * Checks if the TL data starts with gzip_packed constructor (0x3072cfa1).
 * If so, decompresses the inner bytes and writes to out buffer.
 * If not gzip_packed, copies input to output unchanged.
 *
 * @param data   TL payload (possibly gzip_packed).
 * @param len    Payload length.
 * @param out    Output buffer for unwrapped data.
 * @param max_len Output buffer capacity.
 * @param out_len Receives actual output length.
 * @return 0 on success, -1 on error.
 */
int rpc_unwrap_gzip(const uint8_t *data, size_t len,
                    uint8_t *out, size_t max_len, size_t *out_len);

/**
 * @brief Single message from a msg_container.
 */
typedef struct {
    uint64_t msg_id;
    uint32_t seqno;
    uint32_t body_len;
    const uint8_t *body;  /**< Points into the original buffer (not owned). */
} RpcContainerMsg;

/**
 * @brief Parse a msg_container (0x73f1f8dc) into individual messages.
 *
 * If the data is not a msg_container, returns count=1 with the entire
 * payload as a single message.
 *
 * @param data     TL payload.
 * @param len      Payload length.
 * @param msgs     Output array.
 * @param max_msgs Array capacity.
 * @param count    Receives number of messages parsed.
 * @return 0 on success, -1 on error.
 */
int rpc_parse_container(const uint8_t *data, size_t len,
                        RpcContainerMsg *msgs, size_t max_msgs,
                        size_t *count);

/**
 * @brief Parsed RPC error information.
 */
typedef struct {
    int32_t  error_code;        /**< HTTP-like error code (303, 420, etc.) */
    char     error_msg[128];    /**< Error message string (e.g. "FLOOD_WAIT_30") */
    int      migrate_dc;        /**< DC to migrate to (from PHONE/FILE_MIGRATE_X), or -1 */
    int      flood_wait_secs;   /**< Seconds to wait (from FLOOD_WAIT_X), or 0 */
} RpcError;

/**
 * @brief Parse an rpc_error (0x2144ca19) from TL data.
 *
 * Extracts error_code, error_message, and derived fields (migrate_dc,
 * flood_wait_secs). If the TL data does not start with rpc_error constructor,
 * returns -1.
 *
 * @param data TL payload.
 * @param len  Payload length.
 * @param err  Output error struct.
 * @return 0 if rpc_error was parsed, -1 if not an rpc_error or parse failure.
 */
int rpc_parse_error(const uint8_t *data, size_t len, RpcError *err);

/**
 * @brief Check if TL data starts with rpc_result (0xf35c6d01).
 *
 * If so, extracts the req_msg_id and returns a pointer to the inner
 * result payload (which may be rpc_error or a normal response).
 *
 * @param data        TL payload.
 * @param len         Payload length.
 * @param req_msg_id  Output: the request message ID this is a reply to.
 * @param inner       Output: pointer to inner result data.
 * @param inner_len   Output: length of inner result data.
 * @return 0 if rpc_result, -1 if not.
 */
int rpc_unwrap_result(const uint8_t *data, size_t len,
                      uint64_t *req_msg_id,
                      const uint8_t **inner, size_t *inner_len);

#endif /* MTPROTO_RPC_H */
