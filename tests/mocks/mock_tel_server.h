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
 * @brief Arm a one-shot bad_server_salt rejection (TEST-88 alias).
 *
 * Identical to mt_server_set_bad_salt_once but named to match the
 * `mt_server_reply_*` family so the US-37 test suite reads consistently.
 * The next RPC frame the client sends gets a bad_server_salt#edab447b
 * reply; the registered handler is not called; on the client's one-shot
 * retry the flag has cleared and the RPC dispatches normally — exercising
 * classify_service_frame's SVC_BAD_SALT branch end-to-end.
 *
 * @param new_salt  The salt the server wants the client to adopt.
 */
void mt_server_reply_bad_server_salt(uint64_t new_salt);

/**
 * @brief Queue a new_session_created#9ec20908 frame to be sent AHEAD of the
 *        next RPC result.
 *
 * The next RPC the mock server dispatches emits a new_session_created
 * service frame first, then runs the registered handler, which emits the
 * real rpc_result. The client's api_call drains the service frame in its
 * classify_service_frame loop (SVC_SKIP, updates s->server_salt) and then
 * surfaces the real result. Exercises the salt-refresh branch without
 * disconnecting.
 *
 * Uses a fixed, recognisable server_salt so tests can assert that
 * s->server_salt was updated from the frame.
 */
void mt_server_reply_new_session_created(void);

/**
 * @brief Queue a msgs_ack#62d6b459 frame ahead of the next RPC result.
 *
 * msgs_ack is classified SVC_SKIP; the client drains it transparently
 * and proceeds to read the real result queued by the handler.
 *
 * @param ids  Array of msg_ids to ack (opaque to the client — only the
 *             CRC matters for classification).
 * @param n    Number of ids.
 */
void mt_server_reply_msgs_ack(const uint64_t *ids, size_t n);

/**
 * @brief Queue a pong#347773c5 frame ahead of the next RPC result.
 *
 * pong is classified SVC_SKIP (same as msgs_ack). Exercises the second
 * branch of the SKIP `||` chain in classify_service_frame.
 *
 * @param msg_id   Echoed msg_id (ping target).
 * @param ping_id  Echoed ping identifier.
 */
void mt_server_reply_pong(uint64_t msg_id, uint64_t ping_id);

/**
 * @brief Queue a bad_msg_notification#a7eff811 frame ahead of the next RPC
 *        result.
 *
 * The client's classifier returns SVC_ERROR on this frame, so api_call
 * fails (-1) BEFORE reaching the queued real result. The caller can
 * assert that the session struct retains its auth_key and salt — i.e.
 * the error is surfaced without the client dropping the session.
 *
 * @param bad_id  The msg_id the server is complaining about.
 * @param code    Error code (e.g. 16 = msg_id too low, 32 = seqno too low).
 */
void mt_server_reply_bad_msg_notification(uint64_t bad_id, int code);

/**
 * @brief Stack @p count msgs_ack service frames ahead of the next RPC
 *        result.
 *
 * Stress-tests the SERVICE_FRAME_LIMIT drain loop in api_call_once. With
 * count < 8 the client drains them all and still surfaces the real
 * result. With count >= 8 the client hits the loop limit and returns -1.
 *
 * Each queued ack carries a distinct synthetic msg_id; bodies are
 * otherwise identical. Internally just calls mt_server_reply_msgs_ack()
 * @p count times, so the limit on `count` is the queue capacity.
 *
 * @param count  Number of msgs_ack frames to prepend.
 */
void mt_server_stack_service_frames(size_t count);

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

/**
 * @brief Install a responder on auth.sendCode that replies with
 *        `400 PHONE_MIGRATE_<dc_id>` on the next call.
 *
 * Exercises rpc_parse_error's PHONE_MIGRATE_ branch and the login
 * flow's auth.sendCode migration retry. The responder fires for every
 * auth.sendCode until replaced or until mt_server_reset() clears handlers;
 * re-arm the helper (or register a happy-path responder) to change the
 * behaviour on the next dispatch.
 *
 * @param dc_id  DC number to embed in the PHONE_MIGRATE_X suffix.
 */
void mt_server_reply_phone_migrate(int dc_id);

/**
 * @brief Install a responder on auth.signIn that replies with
 *        `303 USER_MIGRATE_<dc_id>` on the next call.
 *
 * Exercises rpc_parse_error's USER_MIGRATE_ branch and the login
 * flow's post-signIn migration handling. Same lifecycle as
 * mt_server_reply_phone_migrate.
 *
 * @param dc_id  DC number to embed in the USER_MIGRATE_X suffix.
 */
void mt_server_reply_user_migrate(int dc_id);

/**
 * @brief Install a responder on auth.sendCode that replies with
 *        `303 NETWORK_MIGRATE_<dc_id>` on the next call.
 *
 * Exercises rpc_parse_error's NETWORK_MIGRATE_ branch. Unlike
 * PHONE_MIGRATE/USER_MIGRATE, NETWORK_MIGRATE is a per-RPC transient
 * signal: the caller retries the same RPC on the named DC without
 * flipping its home DC. Same lifecycle as the sibling helpers.
 *
 * @param dc_id  DC number to embed in the NETWORK_MIGRATE_X suffix.
 */
void mt_server_reply_network_migrate(int dc_id);

/**
 * @brief Install a responder on auth.exportAuthorization that emits
 *        `auth.exportedAuthorization { id, bytes }`.
 *
 * Used by TEST-70 / US-19 (cross-DC auth transfer) to drive the home-DC
 * side of the export/import handshake. The returned token is the
 * opaque bytes the client will feed into auth.importAuthorization on
 * the foreign DC. Same lifecycle as the `mt_server_reply_*` family:
 * the responder fires until replaced or until mt_server_reset() clears
 * the handler table.
 *
 * @param id     Token id the server emits (echoed in the reply).
 * @param bytes  Opaque token bytes (server-chosen, ≤ 1024 bytes so the
 *               client's AUTH_TRANSFER_BYTES_MAX cap is respected).
 * @param len    Length of @p bytes. Must be > 0 and ≤ 1024.
 */
void mt_server_reply_export_authorization(int64_t id,
                                           const uint8_t *bytes, size_t len);

/**
 * @brief Install a responder on auth.importAuthorization.
 *
 * Counterpart of mt_server_reply_export_authorization. The responder
 * emits either `auth.authorization#2ea2c0d4 { user }` or, if @p sign_up
 * is non-zero, the `auth.authorizationSignUpRequired#44747e9a` sentinel
 * that signals the foreign DC holds no account for this user and a
 * fresh signup is required (US-19 `authorizationSignUpRequired` edge
 * case).
 *
 * Same lifecycle as the sibling helpers.
 *
 * @param sign_up  0 → auth.authorization (happy path), non-zero → the
 *                 sign-up-required sentinel.
 */
void mt_server_reply_import_authorization(int sign_up);

/**
 * @brief One-shot override: next auth.importAuthorization reply is an
 *        `rpc_error(401, "AUTH_KEY_INVALID")`.
 *
 * Useful for simulating the US-19 "token expiry race" where the DC4
 * handshake takes too long and the exported token has already expired
 * server-side. The override auto-clears after a single dispatch; the
 * standard auth.importAuthorization responder (if any) fires on the
 * retry. Call before (or instead of)
 * mt_server_reply_import_authorization.
 */
void mt_server_reply_import_authorization_auth_key_invalid_once(void);

/* ---- TEST-71 / US-20 cold-boot MTProto handshake helpers ----
 *
 * These helpers install responders on the mock's *unencrypted* frame path
 * (auth_key_id == 0) so functional tests can drive the DH key exchange
 * against the production mtproto_auth.c code path with real OpenSSL.
 *
 * Scope note: the full set_client_DH → dh_gen_ok round trip requires the
 * mock to decrypt the client's RSA_PAD-encrypted inner_data, which needs
 * Telegram's RSA private key (not shipped — see telegram_server_key.h).
 * The helpers therefore cover step 1 (req_pq_multi → resPQ) exhaustively
 * plus the enumerated negative-path variants tests/functional/
 * test_handshake_cold_boot.c exercises. Steps 3/4 (server_DH_params_ok,
 * dh_gen_ok) are unreachable without modifying production to accept a
 * test RSA key; functional tests assert the expected mid-handshake
 * failure modes instead.
 */

/** resPQ response modes for mt_server_simulate_cold_boot. */
typedef enum {
    MT_COLD_BOOT_OK,                 /**< Valid resPQ with Telegram fingerprint. */
    MT_COLD_BOOT_BAD_FINGERPRINT,    /**< resPQ lists a fingerprint the client doesn't know. */
    MT_COLD_BOOT_WRONG_CONSTRUCTOR,  /**< resPQ crc replaced with garbage. */
    MT_COLD_BOOT_NONCE_TAMPER,       /**< resPQ echoes back a nonce != client's. */
    MT_COLD_BOOT_BAD_PQ              /**< resPQ pq is unfactorisable (prime). */
} MtColdBootMode;

/**
 * @brief Arm the mock to reply to an incoming req_pq_multi#be7e8ef1
 *        handshake frame with a resPQ#05162463 response.
 *
 * The mock reads the client's 16-byte nonce from the unencrypted frame,
 * echoes it back, picks its own 16-byte server_nonce (deterministic for
 * reproducibility), and emits pq = 21 (= 3 * 7) plus the canonical
 * Telegram RSA fingerprint. @p mode selects the response variant:
 *   - MT_COLD_BOOT_OK            → valid resPQ the client accepts
 *   - MT_COLD_BOOT_BAD_FINGERPRINT → fingerprint 0xDEADBEEF...
 *   - MT_COLD_BOOT_WRONG_CONSTRUCTOR → constructor 0xDEADBEEFU
 *   - MT_COLD_BOOT_NONCE_TAMPER  → echoed nonce XOR 0xFF per byte
 *   - MT_COLD_BOOT_BAD_PQ        → pq = 0xFFFFFFFFFFFFFFC5 (prime)
 *
 * Clears any seeded auth_key so the client can re-run the handshake on
 * a genuinely empty session. Call @p mt_server_reset beforehand.
 */
void mt_server_simulate_cold_boot(MtColdBootMode mode);

/**
 * @brief After mt_server_simulate_cold_boot(MT_COLD_BOOT_OK), arm the
 *        mock to additionally reply to the client's req_DH_params
 *        frame with a server_DH_params_ok envelope whose AES-IGE inner
 *        payload is random bytes.
 *
 * The client's auth_step_parse_dh decrypts the payload with a temp key
 * derived from new_nonce+server_nonce, sees a garbage inner_crc, and
 * returns -1. This drives the full mtproto_auth_key_gen orchestrator
 * through steps 1 + 2 + 3 and proves the failure is handled cleanly
 * (no partial session persistence).
 */
void mt_server_simulate_cold_boot_through_step3(void);

/**
 * @brief TEST-72: Arm the mock for a full MTProto DH handshake using the
 *        test-only RSA key pair embedded in tests/mocks/telegram_server_key.c.
 *
 * In this mode the mock:
 *   1. Responds to req_pq_multi with a valid resPQ (test fingerprint).
 *   2. RSA-PAD-decrypts req_DH_params (using the test RSA private key),
 *      extracts new_nonce, generates g=2 and a 256-bit safe prime,
 *      computes g_a=2^b mod p, and sends a valid server_DH_params_ok.
 *   3. Decrypts set_client_DH_params, computes auth_key = g_b^b mod p,
 *      verifies new_nonce_hash1, sends dh_gen_ok, persists the session.
 *
 * On success mtproto_auth_key_gen returns 0, s.has_auth_key == 1, and
 * session.bin is written to $HOME/.config/tg-cli/.
 *
 * WARNING: TEST_ONLY — never call in production. Uses a test private key.
 */
void mt_server_simulate_full_dh_handshake(void);

/**
 * @brief Counter of req_pq_multi frames seen since last reset.
 *
 * Lets tests assert "the handshake restarted once" without having to
 * scan the raw crc ring buffer manually.
 */
int mt_server_handshake_req_pq_count(void);

/**
 * @brief Counter of req_DH_params frames seen since last reset.
 */
int mt_server_handshake_req_dh_count(void);

/**
 * @brief Counter of set_client_DH_params frames seen since last reset.
 *
 * Incremented only in full-DH mode (mt_server_simulate_full_dh_handshake).
 */
int mt_server_handshake_set_client_dh_count(void);

#endif /* MOCK_TEL_SERVER_H */
