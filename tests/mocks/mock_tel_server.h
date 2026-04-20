/**
 * @file mock_tel_server.h
 * @brief In-process Telegram server emulator for functional tests.
 *
 * Both the client (production code under test) and this server run real
 * OpenSSL — the only thing faked is the TCP socket, which is bridged by
 * `tests/mocks/socket.c`. That means every functional test exercises the
 * exact production AES-IGE + SHA-256 code paths (ADR-0004 keeps the
 * `crypto_*` wrappers real in the functional runner).
 *
 * Usage pattern:
 *   mt_server_init();
 *   mt_server_seed_session(2, my_auth_key, &salt, &sid);
 *   mt_server_expect(MESSAGES_GET_DIALOGS_CRC, on_get_dialogs, NULL);
 *   domain_get_dialogs(...);  // real client code
 *   mt_server_reset();
 *
 * Wire format handled on both directions:
 *   abridged_len_prefix + auth_key_id(8) + msg_key(16) + AES-IGE(
 *       salt(8) session_id(8) msg_id(8) seq_no(4) len(4) body(len) padding
 *   )
 *
 * Transparent unwrapping on the client side for:
 *   invokeWithLayer#da9b0d0d, initConnection#c1cd5ea9 (so the registered
 *   responder only sees the inner RPC CRC).
 */

#ifndef MOCK_TEL_SERVER_H
#define MOCK_TEL_SERVER_H

#include <stddef.h>
#include <stdint.h>

#define MT_SERVER_AUTH_KEY_SIZE 256

/**
 * @brief Reset server state + clear mock_socket + arm the on-sent hook.
 *
 * Call before every test. Wipes RPC dispatch table, per-request parse
 * cursor, pending server updates, and sets up a fresh internal sessionid.
 */
void mt_server_reset(void);

/**
 * @brief One-time init (no-op if called repeatedly).
 */
void mt_server_init(void);

/**
 * @brief Seed a valid MTProto session on disk and arm the server with
 *        the matching auth_key + session_id + server_salt.
 *
 * Writes `~/.config/tg-cli/session.bin` in the current process (the test
 * is expected to point $HOME at a temp dir beforehand). The client reads
 * this file on boot and skips the DH handshake, arriving at the first
 * encrypted RPC with keys that match what the server will use.
 *
 * @param dc_id          Home DC to tag in the session file (typically 2).
 * @param auth_key_out   Receives the 256-byte auth_key. May be NULL.
 * @param salt_out       Receives the server salt. May be NULL.
 * @param session_id_out Receives the client session id. May be NULL.
 * @return 0 on success, -1 on IO error.
 */
int mt_server_seed_session(int dc_id,
                           uint8_t auth_key_out[MT_SERVER_AUTH_KEY_SIZE],
                           uint64_t *salt_out,
                           uint64_t *session_id_out);

/**
 * @brief RPC context passed to a responder callback.
 *
 * The request body pointer is owned by the server's per-frame scratch
 * buffer and is valid only for the duration of the responder call — a
 * responder that needs the bytes beyond its own return must copy them.
 */
typedef struct {
    uint64_t      req_msg_id;
    uint32_t      req_crc;
    const uint8_t *req_body;   /* includes the CRC prefix */
    size_t        req_body_len;
    void         *user_ctx;
} MtRpcContext;

typedef void (*MtResponder)(MtRpcContext *ctx);

/**
 * @brief Register a responder keyed by TL constructor CRC.
 *
 * When the client sends a frame whose innermost RPC (after any
 * invokeWithLayer / initConnection wrapping) starts with @p crc, the
 * responder fires. Repeat calls with the same CRC replace the handler.
 */
void mt_server_expect(uint32_t crc, MtResponder fn, void *ctx);

/**
 * @brief Emit an rpc_result#f35c6d01 from within a responder.
 *
 * @param ctx       The context passed to the responder.
 * @param body      TL-encoded inner result (e.g. a `config` or `dialogs`).
 * @param body_len  Length in bytes.
 */
void mt_server_reply_result(const MtRpcContext *ctx,
                            const uint8_t *body, size_t body_len);

/**
 * @brief Emit an rpc_error#2144ca19 from within a responder.
 */
void mt_server_reply_error(const MtRpcContext *ctx,
                           int32_t error_code, const char *error_msg);

/**
 * @brief Emit `rpc_result { gzip_packed { body } }` from within a responder.
 *
 * The payload @p body is gzipped (deflate stored blocks, no Huffman compression
 * — sufficient for test fixtures without pulling in zlib) and wrapped as a
 * gzip_packed#3072cfa1 TL object, then placed inside an rpc_result. Exercises
 * the production `rpc_unwrap_gzip` path end-to-end.
 *
 * @param ctx       The context passed to the responder.
 * @param body      TL-encoded inner result to compress.
 * @param body_len  Length in bytes.
 */
void mt_server_reply_gzip_wrapped_result(const MtRpcContext *ctx,
                                          const uint8_t *body, size_t body_len);

/**
 * @brief Emit `rpc_result { gzip_packed { GARBAGE } }` from within a responder.
 *
 * Replaces the gzip body with bytes that fail tinf's header / CRC checks, so
 * `rpc_unwrap_gzip` returns -1 and the domain call bubbles the error up.
 */
void mt_server_reply_gzip_corrupt(const MtRpcContext *ctx);

/**
 * @brief Emit an `msg_container` enclosing @p n_children pre-serialised bodies.
 *
 * Each child is wrapped with a (msg_id, seqno, body_len) triple per the
 * MTProto msg_container wire layout. Children are placed in the order given.
 * The outer frame is the container itself — no rpc_result wrapper — so the
 * caller drives `rpc_parse_container` against the received plaintext rather
 * than through `api_call()`.
 *
 * @param ctx         The context passed to the responder (used only for
 *                    envelope msg_id/salt, not the children).
 * @param children    Array of TL bodies (each includes its own CRC prefix).
 * @param child_lens  Array of body lengths, one per child. Each MUST be a
 *                    multiple of 4 — TL bodies are always 4-byte aligned.
 * @param n_children  Number of children.
 */
void mt_server_reply_msg_container(const MtRpcContext *ctx,
                                    const uint8_t *const *children,
                                    const size_t *child_lens,
                                    size_t n_children);

/**
 * @brief Queue a server-initiated TL update (e.g. updates_difference) so it
 *        lands on the next recv call. Useful for watch-loop tests.
 */
void mt_server_push_update(const uint8_t *tl, size_t tl_len);

/**
 * @brief Arm a one-shot bad_server_salt rejection.
 *
 * The next RPC frame received from the client is answered with a
 * bad_server_salt#edab447b service frame (bad_msg_id = that frame's msg_id,
 * new_server_salt = @p new_salt) instead of being dispatched. The
 * registered handler is not called. On the client's retry, the flag has
 * cleared and the RPC dispatches normally.
 */
void mt_server_set_bad_salt_once(uint64_t new_salt);

/**
 * @brief Arm a one-shot wrong session_id injection.
 *
 * The next reply frame sent by the mock server will contain a session_id
 * that differs from the client's expected session_id (all bits flipped).
 * rpc_recv_encrypted() must reject the frame and return -1. The flag is
 * cleared automatically after the first reply is emitted.
 */
void mt_server_set_wrong_session_id_once(void);

/**
 * @brief Number of successfully dispatched RPC frames since last reset.
 *        Handy for asserting "the client made exactly N calls".
 */
int mt_server_rpc_call_count(void);

/**
 * @brief Number of raw frames received whose leading CRC matches @p crc.
 *
 * Counts both unencrypted frames (auth_key_id == 0, e.g. DH handshake
 * messages req_pq, req_DH_params, set_client_DH_params) and encrypted
 * inner-RPC frames.  Useful for asserting that the handshake was (or was
 * not) performed.
 *
 * @param crc  TL constructor CRC to search for.
 * @return     Number of frames whose innermost CRC equals @p crc.
 */
int mt_server_request_crc_count(uint32_t crc);

/**
 * @brief Arm a one-shot parse-state reset.
 *
 * When set, the next time the mock's on_client_sent callback encounters the
 * 0xEF abridged-transport marker byte at the current parse cursor, it treats
 * it as a fresh connection start (resets saw_marker = 0 and advances past the
 * marker) rather than attempting to decode it as a frame length prefix.
 *
 * Use this before a test that causes production code to open a second
 * transport connection (e.g. NETWORK_MIGRATE cross-DC retry) so that the
 * second connection's frames are parsed correctly by the same mock server.
 */
void mt_server_arm_reconnect(void);

/**
 * @brief Seed an additional DC session on disk using the current auth key.
 *
 * Call after mt_server_seed_session() to pre-populate a secondary-DC entry
 * in the session store so that dc_session_open(dc_id) takes the fast path
 * (no DH handshake) in tests that exercise cross-DC migration.
 *
 * @param dc_id  The DC number to seed (e.g. 4 for NETWORK_MIGRATE_4 tests).
 * @return 0 on success, -1 if the primary session has not been seeded yet.
 */
int mt_server_seed_extra_dc(int dc_id);

#endif /* MOCK_TEL_SERVER_H */
