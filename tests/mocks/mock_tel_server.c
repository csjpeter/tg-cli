/**
 * @file mock_tel_server.c
 * @brief In-process Telegram server emulator — see mock_tel_server.h.
 */

#include "mock_tel_server.h"
#include "mock_socket.h"

#include "crypto.h"
#include "mtproto_crypto.h"
#include "mtproto_session.h"
#include "tl_serial.h"
#include "tl_skip.h"
#include "session_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CRC_invokeWithLayer  0xda9b0d0dU
#define CRC_initConnection   0xc1cd5ea9U
#define CRC_rpc_result       0xf35c6d01U
#define CRC_rpc_error        0x2144ca19U

#define MT_MAX_HANDLERS      64
#define MT_MAX_UPDATES       32
#define MT_FRAME_MAX         (256 * 1024)
#define MT_CRC_RING_SIZE     256

typedef struct {
    uint32_t    crc;
    MtResponder fn;
    void       *ctx;
    int         used;
} MtHandler;

typedef struct {
    uint8_t *bytes;
    size_t   len;
} MtBlob;

static struct {
    int         initialized;
    int         seeded;

    uint8_t     auth_key[MT_SERVER_AUTH_KEY_SIZE];
    uint64_t    auth_key_id;
    uint64_t    server_salt;
    uint64_t    session_id;

    uint64_t    next_server_msg_id;
    uint32_t    seq_no;

    size_t      parse_cursor;    /* next byte in mock_socket sent-buffer */
    int         saw_marker;      /* consumed the initial 0xEF yet? */

    MtHandler   handlers[MT_MAX_HANDLERS];

    MtBlob      pending_updates[MT_MAX_UPDATES];
    size_t      pending_update_count;

    int         rpc_call_count;

    /* Scratch state that reply builders read to know which client msg_id
     * they are answering. Valid only during responder execution. */
    uint64_t    current_req_msg_id;

    /* One-shot bad_server_salt injection. When non-zero, the next dispatched
     * RPC triggers a bad_server_salt reply instead of running the handler. */
    int         bad_salt_once_pending;
    uint64_t    bad_salt_new_salt;

    /* One-shot reconnect detection: when set, the next 0xEF byte at the
     * parse cursor is treated as a fresh connection marker rather than a
     * frame length prefix. Cleared automatically after use. */
    int         reconnect_pending;

    /* Ring buffer of leading CRCs from every frame received (both
     * unencrypted handshake frames and encrypted inner-RPC frames).
     * Used by mt_server_request_crc_count(). */
    uint32_t    crc_ring[MT_CRC_RING_SIZE];
    size_t      crc_ring_count;   /* total recorded; wraps at MT_CRC_RING_SIZE */
} g_srv;

/* ---- forward decls ---- */
static void on_client_sent(const uint8_t *buf, size_t len);
static void dispatch_frame(const uint8_t *plain, size_t plain_len);
static void unwrap_and_dispatch(uint64_t req_msg_id,
                                const uint8_t *body, size_t body_len);
static void encrypt_and_queue(const uint8_t *plain, size_t plain_len);
static void queue_frame(const uint8_t *body, size_t body_len);
static uint64_t derive_auth_key_id(const uint8_t *auth_key);
static uint64_t make_server_msg_id(void);

/* ================================================================ */
/* Public API                                                       */
/* ================================================================ */

void mt_server_init(void) {
    if (g_srv.initialized) return;
    memset(&g_srv, 0, sizeof(g_srv));
    g_srv.initialized = 1;
}

void mt_server_reset(void) {
    /* Preserve initialized flag, wipe everything else. */
    mock_socket_set_on_sent(NULL);
    for (size_t i = 0; i < g_srv.pending_update_count; ++i) {
        free(g_srv.pending_updates[i].bytes);
    }
    memset(&g_srv, 0, sizeof(g_srv));
    g_srv.initialized = 1;

    mock_socket_reset();
    mock_socket_set_on_sent(on_client_sent);
}

int mt_server_seed_session(int dc_id,
                           uint8_t auth_key_out[MT_SERVER_AUTH_KEY_SIZE],
                           uint64_t *salt_out,
                           uint64_t *session_id_out) {
    /* Deterministic-ish auth_key so tests stay reproducible across runs. */
    for (int i = 0; i < MT_SERVER_AUTH_KEY_SIZE; ++i) {
        g_srv.auth_key[i] = (uint8_t)((i * 31 + 7) & 0xFFu);
    }
    g_srv.auth_key_id  = derive_auth_key_id(g_srv.auth_key);
    g_srv.server_salt  = 0xABCDEF0123456789ULL;
    g_srv.session_id   = 0x1122334455667788ULL;
    g_srv.next_server_msg_id = 0;
    g_srv.seq_no = 0;

    MtProtoSession s;
    mtproto_session_init(&s);
    mtproto_session_set_auth_key(&s, g_srv.auth_key);
    mtproto_session_set_salt(&s, g_srv.server_salt);
    s.session_id = g_srv.session_id;

    if (session_store_save(&s, dc_id) != 0) return -1;

    if (auth_key_out)   memcpy(auth_key_out, g_srv.auth_key, MT_SERVER_AUTH_KEY_SIZE);
    if (salt_out)       *salt_out       = g_srv.server_salt;
    if (session_id_out) *session_id_out = g_srv.session_id;

    g_srv.seeded = 1;
    return 0;
}

void mt_server_expect(uint32_t crc, MtResponder fn, void *ctx) {
    /* Replace existing handler for the same CRC if present. */
    for (size_t i = 0; i < MT_MAX_HANDLERS; ++i) {
        if (g_srv.handlers[i].used && g_srv.handlers[i].crc == crc) {
            g_srv.handlers[i].fn  = fn;
            g_srv.handlers[i].ctx = ctx;
            return;
        }
    }
    for (size_t i = 0; i < MT_MAX_HANDLERS; ++i) {
        if (!g_srv.handlers[i].used) {
            g_srv.handlers[i].used = 1;
            g_srv.handlers[i].crc  = crc;
            g_srv.handlers[i].fn   = fn;
            g_srv.handlers[i].ctx  = ctx;
            return;
        }
    }
    /* Table full — test setup bug. Abort loudly. */
    fprintf(stderr, "mt_server_expect: handler table full\n");
    abort();
}

void mt_server_reply_result(const MtRpcContext *ctx,
                            const uint8_t *body, size_t body_len) {
    if (!ctx) return;
    /* rpc_result#f35c6d01 = req_msg_id:long result:Object */
    size_t wrapped_len = 4 + 8 + body_len;
    uint8_t *wrapped = (uint8_t *)malloc(wrapped_len);
    if (!wrapped) return;
    wrapped[0] = (uint8_t)(CRC_rpc_result);
    wrapped[1] = (uint8_t)(CRC_rpc_result >> 8);
    wrapped[2] = (uint8_t)(CRC_rpc_result >> 16);
    wrapped[3] = (uint8_t)(CRC_rpc_result >> 24);
    for (int i = 0; i < 8; ++i) {
        wrapped[4 + i] = (uint8_t)((ctx->req_msg_id >> (i * 8)) & 0xFFu);
    }
    memcpy(wrapped + 12, body, body_len);
    queue_frame(wrapped, wrapped_len);
    free(wrapped);
}

void mt_server_reply_error(const MtRpcContext *ctx,
                           int32_t error_code, const char *error_msg) {
    if (!ctx) return;
    /* rpc_error#2144ca19 = error_code:int error_message:string */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_rpc_error);
    tl_write_int32(&w, error_code);
    tl_write_string(&w, error_msg ? error_msg : "");
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

void mt_server_push_update(const uint8_t *tl, size_t tl_len) {
    if (g_srv.pending_update_count >= MT_MAX_UPDATES) return;
    uint8_t *copy = (uint8_t *)malloc(tl_len);
    if (!copy) return;
    memcpy(copy, tl, tl_len);
    g_srv.pending_updates[g_srv.pending_update_count].bytes = copy;
    g_srv.pending_updates[g_srv.pending_update_count].len   = tl_len;
    g_srv.pending_update_count++;
}

int mt_server_rpc_call_count(void) { return g_srv.rpc_call_count; }

int mt_server_request_crc_count(uint32_t crc) {
    int count = 0;
    size_t n = g_srv.crc_ring_count;
    if (n > MT_CRC_RING_SIZE) n = MT_CRC_RING_SIZE;
    for (size_t i = 0; i < n; ++i) {
        if (g_srv.crc_ring[i] == crc) count++;
    }
    return count;
}

void mt_server_arm_reconnect(void) {
    g_srv.reconnect_pending = 1;
}

int mt_server_seed_extra_dc(int dc_id) {
    if (!g_srv.seeded) return -1;
    MtProtoSession s;
    mtproto_session_init(&s);
    mtproto_session_set_auth_key(&s, g_srv.auth_key);
    mtproto_session_set_salt(&s, g_srv.server_salt);
    s.session_id = g_srv.session_id;
    return session_store_save_dc(dc_id, &s);
}

void mt_server_set_bad_salt_once(uint64_t new_salt) {
    g_srv.bad_salt_once_pending = 1;
    g_srv.bad_salt_new_salt = new_salt;
}

/* ================================================================ */
/* Internals                                                         */
/* ================================================================ */

static uint64_t derive_auth_key_id(const uint8_t *auth_key) {
    uint8_t hash[32];
    crypto_sha256(auth_key, MT_SERVER_AUTH_KEY_SIZE, hash);
    uint64_t id = 0;
    for (int i = 0; i < 8; ++i) id |= ((uint64_t)hash[24 + i]) << (i * 8);
    return id;
}

static uint64_t make_server_msg_id(void) {
    /* Monotonic even msg_ids (server → client use low-bit 1 in real MT,
     * but the client tolerates either — we just need monotonic). */
    uint64_t now = (uint64_t)time(NULL) << 32;
    if (now <= g_srv.next_server_msg_id) now = g_srv.next_server_msg_id + 4;
    now &= ~((uint64_t)3);
    now |= 1;  /* server → client msg_id is odd */
    g_srv.next_server_msg_id = now;
    return now;
}

/* Read an abridged length prefix from buf[cursor..len]. Returns:
 *   bytes consumed by the prefix on success (1 or 4)
 *   0 if not enough bytes yet
 * On success, *payload_len receives the payload size in bytes. */
static size_t read_abridged_prefix(const uint8_t *buf, size_t len, size_t cursor,
                                    size_t *payload_len) {
    if (cursor >= len) return 0;
    uint8_t first = buf[cursor];
    if (first < 0x7F) {
        *payload_len = (size_t)first * 4;
        return 1;
    }
    if (cursor + 4 > len) return 0;
    size_t wire = (size_t)buf[cursor + 1]
                | ((size_t)buf[cursor + 2] << 8)
                | ((size_t)buf[cursor + 3] << 16);
    *payload_len = wire * 4;
    return 4;
}

static void on_client_sent(const uint8_t *buf, size_t len) {
    if (!g_srv.seeded) return;

    while (g_srv.parse_cursor < len) {
        /* Step 0 — consume the initial 0xEF marker on the very first byte.
         * Also handle a one-shot reconnect: if reconnect_pending is set and
         * the next byte is 0xEF, treat it as a new-connection marker (reset
         * parse state) so that a second transport session opened by production
         * code (e.g. cross-DC NETWORK_MIGRATE retry) is parsed cleanly. */
        if (!g_srv.saw_marker) {
            if (buf[g_srv.parse_cursor] != 0xEFu) {
                /* Unexpected first byte — not an abridged connection. */
                return;
            }
            g_srv.parse_cursor++;
            g_srv.saw_marker = 1;
            continue;
        }
        if (g_srv.reconnect_pending && buf[g_srv.parse_cursor] == 0xEFu) {
            /* A second transport connection opened by the client. Reset the
             * parser to treat this 0xEF as the new connection's abridged
             * marker. */
            g_srv.reconnect_pending = 0;
            g_srv.saw_marker = 0;
            continue;   /* re-enter the saw_marker=0 branch above */
        }

        size_t payload_len = 0;
        size_t prefix_bytes = read_abridged_prefix(buf, len, g_srv.parse_cursor,
                                                    &payload_len);
        if (prefix_bytes == 0) return;  /* need more data */
        if (payload_len == 0) {
            /* Idle keep-alive — skip. */
            g_srv.parse_cursor += prefix_bytes;
            continue;
        }
        if (g_srv.parse_cursor + prefix_bytes + payload_len > len) {
            return;  /* wait for rest of payload */
        }

        const uint8_t *frame = buf + g_srv.parse_cursor + prefix_bytes;
        g_srv.parse_cursor += prefix_bytes + payload_len;

        /* Frame = auth_key_id(8) + msg_key(16) + ciphertext */
        if (payload_len < 24) continue;
        uint64_t key_id = 0;
        for (int i = 0; i < 8; ++i) key_id |= ((uint64_t)frame[i]) << (i * 8);
        if (key_id != g_srv.auth_key_id) {
            /* Unencrypted handshake frame: auth_key_id == 0.
             * Record the leading CRC for mt_server_request_crc_count().
             * Unencrypted layout: key_id(8) + msg_id(8) + msg_len(4) + crc(4) */
            if (key_id == 0 && payload_len >= 24) {
                uint32_t raw_crc = (uint32_t)frame[20]
                                 | ((uint32_t)frame[21] << 8)
                                 | ((uint32_t)frame[22] << 16)
                                 | ((uint32_t)frame[23] << 24);
                size_t slot = g_srv.crc_ring_count % MT_CRC_RING_SIZE;
                g_srv.crc_ring[slot] = raw_crc;
                g_srv.crc_ring_count++;
            } else {
                fprintf(stderr, "mt_server: auth_key_id mismatch "
                        "(got %016llx, want %016llx)\n",
                        (unsigned long long)key_id,
                        (unsigned long long)g_srv.auth_key_id);
            }
            continue;
        }

        uint8_t msg_key[16];
        memcpy(msg_key, frame + 8, 16);
        const uint8_t *cipher = frame + 24;
        size_t cipher_len = payload_len - 24;

        uint8_t *plain = (uint8_t *)malloc(cipher_len);
        if (!plain) continue;
        size_t plain_len = 0;

        int rc = mtproto_decrypt(cipher, cipher_len,
                                 g_srv.auth_key, msg_key, 0,
                                 plain, &plain_len);
        if (rc != 0) {
            fprintf(stderr, "mt_server: mtproto_decrypt failed\n");
            free(plain);
            continue;
        }
        dispatch_frame(plain, plain_len);
        free(plain);
    }
}

static void dispatch_frame(const uint8_t *plain, size_t plain_len) {
    /* plaintext header: salt(8) + session_id(8) + msg_id(8) + seq_no(4) + len(4) + body */
    if (plain_len < 32) return;
    uint64_t client_session = 0;
    for (int i = 0; i < 8; ++i) {
        client_session |= ((uint64_t)plain[8 + i]) << (i * 8);
    }
    uint64_t msg_id = 0;
    for (int i = 0; i < 8; ++i) {
        msg_id |= ((uint64_t)plain[16 + i]) << (i * 8);
    }
    uint32_t body_len = 0;
    for (int i = 0; i < 4; ++i) {
        body_len |= ((uint32_t)plain[28 + i]) << (i * 8);
    }
    if (32 + body_len > plain_len) return;
    const uint8_t *body = plain + 32;

    /* First frame carries the client session id — pin it so later msg_container
     * updates use the same one. If the client somehow changes it, that's a
     * protocol error we mirror by just echoing back whatever we last saw. */
    if (g_srv.session_id == 0x1122334455667788ULL) {
        /* keep the seeded value — the client echoes this from session.bin */
    }
    (void)client_session;

    unwrap_and_dispatch(msg_id, body, body_len);
}

static void unwrap_and_dispatch(uint64_t req_msg_id,
                                const uint8_t *body, size_t body_len) {
    if (body_len < 4) return;
    const uint8_t *cur = body;
    size_t remaining = body_len;

    /* Peel invokeWithLayer / initConnection off the front. */
    for (int depth = 0; depth < 3; ++depth) {
        if (remaining < 4) return;
        uint32_t crc = (uint32_t)cur[0] | ((uint32_t)cur[1] << 8)
                     | ((uint32_t)cur[2] << 16) | ((uint32_t)cur[3] << 24);
        if (crc == CRC_invokeWithLayer) {
            if (remaining < 8) return;
            cur += 8; remaining -= 8;   /* CRC + layer:int */
            continue;
        }
        if (crc == CRC_initConnection) {
            /* CRC(4) flags(4) api_id(4) string×6 [proxy:flags.0?] [params:flags.1?] query:!X */
            TlReader r = tl_reader_init(cur, remaining);
            tl_read_uint32(&r);                 /* CRC */
            uint32_t flags = tl_read_uint32(&r);
            tl_read_int32(&r);                  /* api_id */
            for (int i = 0; i < 6; ++i) {
                if (tl_skip_string(&r) != 0) return;
            }
            if (flags & 0x1) {
                /* inputClientProxy#75588b3f server:string port:int */
                tl_read_uint32(&r);
                if (tl_skip_string(&r) != 0) return;
                tl_read_int32(&r);
            }
            if (flags & 0x2) {
                /* jsonObject#7d748d04 — walking JSONValue is out of scope;
                 * the client never sets this flag so bail if we ever see it. */
                return;
            }
            size_t consumed = r.pos;
            cur += consumed; remaining -= consumed;
            continue;
        }
        break;
    }

    if (remaining < 4) return;
    uint32_t inner_crc = (uint32_t)cur[0] | ((uint32_t)cur[1] << 8)
                       | ((uint32_t)cur[2] << 16) | ((uint32_t)cur[3] << 24);

    /* One-shot bad_server_salt injection — bounces the client back with a
     * fresh salt and forces it to resend. The handler is not called on this
     * round; it fires on the retry. */
    if (g_srv.bad_salt_once_pending) {
        g_srv.bad_salt_once_pending = 0;
        /* bad_server_salt#edab447b bad_msg_id:long bad_msg_seqno:int
         *                          error_code:int new_server_salt:long */
        uint8_t buf[4 + 8 + 4 + 4 + 8];
        uint32_t crc = 0xedab447bU;
        for (int i = 0; i < 4; ++i) buf[i] = (uint8_t)(crc >> (i * 8));
        for (int i = 0; i < 8; ++i) buf[4 + i] = (uint8_t)(req_msg_id >> (i * 8));
        /* bad_msg_seqno + error_code 48 (= incorrect server salt) — values
         * are informational only for the client's logger. */
        int32_t seq0 = 0;
        for (int i = 0; i < 4; ++i) buf[12 + i] = (uint8_t)(seq0 >> (i * 8));
        int32_t ec = 48;
        for (int i = 0; i < 4; ++i) buf[16 + i] = (uint8_t)(ec >> (i * 8));
        for (int i = 0; i < 8; ++i) {
            buf[20 + i] = (uint8_t)(g_srv.bad_salt_new_salt >> (i * 8));
        }
        queue_frame(buf, sizeof(buf));
        /* Update server salt so subsequent responses use what the client now
         * expects — the client discards the inbound salt, so this is purely
         * cosmetic (a real server would). */
        g_srv.server_salt = g_srv.bad_salt_new_salt;
        return;
    }

    /* Record the inner CRC in the ring buffer. */
    {
        size_t slot = g_srv.crc_ring_count % MT_CRC_RING_SIZE;
        g_srv.crc_ring[slot] = inner_crc;
        g_srv.crc_ring_count++;
    }

    /* Record + invoke handler. */
    g_srv.rpc_call_count++;
    g_srv.current_req_msg_id = req_msg_id;

    for (size_t i = 0; i < MT_MAX_HANDLERS; ++i) {
        if (g_srv.handlers[i].used && g_srv.handlers[i].crc == inner_crc) {
            MtRpcContext ctx = {
                .req_msg_id   = req_msg_id,
                .req_crc      = inner_crc,
                .req_body     = cur,
                .req_body_len = remaining,
                .user_ctx     = g_srv.handlers[i].ctx,
            };
            g_srv.handlers[i].fn(&ctx);
            return;
        }
    }

    /* No handler → auto rpc_error so the test fails explicitly rather than
     * hanging on recv. */
    MtRpcContext ctx = {
        .req_msg_id = req_msg_id,
        .req_crc    = inner_crc,
        .req_body   = cur,
        .req_body_len = remaining,
        .user_ctx   = NULL,
    };
    char buf[64];
    snprintf(buf, sizeof(buf), "NO_HANDLER_CRC_%08x", inner_crc);
    mt_server_reply_error(&ctx, 500, buf);
}

static void queue_frame(const uint8_t *body, size_t body_len) {
    /* Build plaintext header: salt + session_id + msg_id + seq_no + len + body + padding. */
    TlWriter plain;
    tl_writer_init(&plain);
    tl_write_uint64(&plain, g_srv.server_salt);
    tl_write_uint64(&plain, g_srv.session_id);
    tl_write_uint64(&plain, make_server_msg_id());
    /* seq_no: bump by 2 for content-related, start at 1 so first is 1, 3, 5… */
    g_srv.seq_no += 2;
    tl_write_uint32(&plain, g_srv.seq_no - 1);
    tl_write_uint32(&plain, (uint32_t)body_len);
    tl_write_raw(&plain, body, body_len);

    encrypt_and_queue(plain.data, plain.len);
    tl_writer_free(&plain);
}

static void encrypt_and_queue(const uint8_t *plain, size_t plain_len) {
    uint8_t *enc = (uint8_t *)malloc(plain_len + 1024);
    if (!enc) return;
    size_t enc_len = 0;
    uint8_t msg_key[16];
    mtproto_encrypt(plain, plain_len, g_srv.auth_key, 8, enc, &enc_len, msg_key);

    size_t wire_len = 8 + 16 + enc_len;
    uint8_t *wire = (uint8_t *)malloc(wire_len);
    if (!wire) { free(enc); return; }
    for (int i = 0; i < 8; ++i) {
        wire[i] = (uint8_t)((g_srv.auth_key_id >> (i * 8)) & 0xFFu);
    }
    memcpy(wire + 8, msg_key, 16);
    memcpy(wire + 24, enc, enc_len);
    free(enc);

    /* Abridged length prefix (in 4-byte units). */
    size_t units = wire_len / 4;
    if (units < 0x7F) {
        uint8_t p = (uint8_t)units;
        mock_socket_append_response(&p, 1);
    } else {
        uint8_t p[4];
        p[0] = 0x7F;
        p[1] = (uint8_t)(units & 0xFFu);
        p[2] = (uint8_t)((units >> 8) & 0xFFu);
        p[3] = (uint8_t)((units >> 16) & 0xFFu);
        mock_socket_append_response(p, 4);
    }
    mock_socket_append_response(wire, wire_len);
    free(wire);
}
