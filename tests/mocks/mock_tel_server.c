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
#include "tinf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CRC_invokeWithLayer  0xda9b0d0dU
#define CRC_initConnection   0xc1cd5ea9U
#define CRC_rpc_result       0xf35c6d01U
#define CRC_rpc_error        0x2144ca19U
#define CRC_gzip_packed      0x3072cfa1U
#define CRC_msg_container    0x73f1f8dcU

#define MT_MAX_HANDLERS      64
#define MT_MAX_UPDATES       32
#define MT_FRAME_MAX         (256 * 1024)
#define MT_CRC_RING_SIZE     256
#define MT_MAX_PENDING_SVC   16   /* stacked service frames ahead of next reply */

/* TL CRCs for the service frames TEST-88 exercises. Keep these local so
 * the mock server does not have to link against src/core/tl_registry.h. */
#define CRC_bad_server_salt      0xedab447bU
#define CRC_bad_msg_notification 0xa7eff811U
#define CRC_new_session_created  0x9ec20908U
#define CRC_msgs_ack             0x62d6b459U
#define CRC_pong                 0x347773c5U
#define CRC_vector               0x1cb5c415U

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

    /* One-shot wrong session_id injection. When set, the next reply frame
     * uses a deliberately wrong session_id in the plaintext header so that
     * rpc_recv_encrypted() rejects it. Cleared automatically after use. */
    int         wrong_session_id_once_pending;

    /* Ring buffer of leading CRCs from every frame received (both
     * unencrypted handshake frames and encrypted inner-RPC frames).
     * Used by mt_server_request_crc_count(). */
    uint32_t    crc_ring[MT_CRC_RING_SIZE];
    size_t      crc_ring_count;   /* total recorded; wraps at MT_CRC_RING_SIZE */

    /* Stack of service frames to send ahead of the next real reply.
     * Each entry holds a raw TL body (with leading CRC). Drained in
     * unwrap_and_dispatch before the registered handler runs. */
    MtBlob      pending_service_frames[MT_MAX_PENDING_SVC];
    size_t      pending_service_count;
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
    for (size_t i = 0; i < g_srv.pending_service_count; ++i) {
        free(g_srv.pending_service_frames[i].bytes);
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

/* ---- Minimal gzip encoder (deflate stored blocks) ----
 *
 * Writes @p payload as a sequence of uncompressed deflate blocks ("stored"
 * blocks) wrapped in the gzip member format. This avoids pulling in a real
 * compressor (zlib) for test fixtures — the stored-block path is a
 * well-defined subset of deflate that tinf's inflater handles. Each stored
 * block carries at most 65535 bytes; larger payloads span multiple blocks.
 *
 * Format per RFC 1951 §3.2.4 + RFC 1952:
 *   gzip header (10)  1f 8b 08 00 00 00 00 00 00 ff
 *   deflate stream    [block_header(1) LEN(2 LE) NLEN(2 LE) data(LEN)]+
 *                     (block_header bit0 = BFINAL, bits1-2 = BTYPE=00)
 *   CRC32(payload)    4 bytes LE
 *   ISIZE             4 bytes LE (original length mod 2^32)
 *
 * Returns a heap-allocated buffer the caller must free; NULL on OOM.
 */
static uint8_t *gzip_stored(const uint8_t *payload, size_t payload_len,
                            size_t *out_len) {
    if (!out_len) return NULL;

    /* Upper bound on output size: 10 header + per 65535-byte block
     * overhead (1 + 2 + 2 = 5) + payload + 8 trailer. */
    size_t blocks = (payload_len + 65534) / 65535;
    if (payload_len == 0) blocks = 1;
    size_t cap = 10 + blocks * 5 + payload_len + 8;

    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) return NULL;
    size_t off = 0;

    /* gzip header */
    buf[off++] = 0x1F;  /* ID1 */
    buf[off++] = 0x8B;  /* ID2 */
    buf[off++] = 0x08;  /* CM = deflate */
    buf[off++] = 0x00;  /* FLG = 0 (no name, comment, extra, crc16) */
    buf[off++] = 0x00;  /* MTIME[0] */
    buf[off++] = 0x00;  /* MTIME[1] */
    buf[off++] = 0x00;  /* MTIME[2] */
    buf[off++] = 0x00;  /* MTIME[3] */
    buf[off++] = 0x00;  /* XFL */
    buf[off++] = 0xFF;  /* OS = unknown */

    /* Deflate stored blocks. */
    size_t pos = 0;
    if (payload_len == 0) {
        /* Emit one empty final stored block: header=0x01, LEN=0, NLEN=0xFFFF. */
        buf[off++] = 0x01;
        buf[off++] = 0x00; buf[off++] = 0x00;
        buf[off++] = 0xFF; buf[off++] = 0xFF;
    } else {
        while (pos < payload_len) {
            size_t chunk = payload_len - pos;
            if (chunk > 65535) chunk = 65535;
            int is_final = (pos + chunk == payload_len) ? 1 : 0;
            buf[off++] = (uint8_t)(is_final ? 0x01 : 0x00);
            buf[off++] = (uint8_t)(chunk & 0xFFu);
            buf[off++] = (uint8_t)((chunk >> 8) & 0xFFu);
            uint16_t nlen = (uint16_t)~chunk;
            buf[off++] = (uint8_t)(nlen & 0xFFu);
            buf[off++] = (uint8_t)((nlen >> 8) & 0xFFu);
            memcpy(buf + off, payload + pos, chunk);
            off += chunk;
            pos += chunk;
        }
    }

    /* Trailer: CRC32 of raw payload, then ISIZE mod 2^32. tinf_crc32 matches
     * the standard gzip polynomial. Guard the empty-payload case because
     * tinf_crc32 returns 0 for empty input (which happens to be correct). */
    uint32_t crc = tinf_crc32(payload, (unsigned int)payload_len);
    buf[off++] = (uint8_t)(crc & 0xFFu);
    buf[off++] = (uint8_t)((crc >> 8) & 0xFFu);
    buf[off++] = (uint8_t)((crc >> 16) & 0xFFu);
    buf[off++] = (uint8_t)((crc >> 24) & 0xFFu);
    uint32_t isize = (uint32_t)(payload_len & 0xFFFFFFFFu);
    buf[off++] = (uint8_t)(isize & 0xFFu);
    buf[off++] = (uint8_t)((isize >> 8) & 0xFFu);
    buf[off++] = (uint8_t)((isize >> 16) & 0xFFu);
    buf[off++] = (uint8_t)((isize >> 24) & 0xFFu);

    *out_len = off;
    return buf;
}

void mt_server_reply_gzip_wrapped_result(const MtRpcContext *ctx,
                                          const uint8_t *body,
                                          size_t body_len) {
    if (!ctx || (!body && body_len > 0)) return;
    size_t gz_len = 0;
    uint8_t *gz = gzip_stored(body, body_len, &gz_len);
    if (!gz) return;

    /* gzip_packed#3072cfa1 = packed_data:bytes = Object */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_gzip_packed);
    tl_write_bytes(&w, gz, gz_len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
    free(gz);
}

void mt_server_reply_gzip_corrupt(const MtRpcContext *ctx) {
    if (!ctx) return;
    /* Bytes that fail gzip header validation: wrong magic + too short for
     * the 18-byte minimum tinf requires. rpc_unwrap_gzip propagates the
     * TINF_DATA_ERROR as -1. */
    static const uint8_t garbage[] = {
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22,
        0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00,
        0xDE, 0xAD, 0xBE, 0xEF
    };
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_gzip_packed);
    tl_write_bytes(&w, garbage, sizeof(garbage));
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

void mt_server_reply_msg_container(const MtRpcContext *ctx,
                                    const uint8_t *const *children,
                                    const size_t *child_lens,
                                    size_t n_children) {
    if (!ctx || !children || !child_lens || n_children == 0) return;

    /* msg_container#73f1f8dc messages:vector<message>
     * message { msg_id:long seqno:int bytes:int body:bytes_untyped }
     * Each body is concatenated raw (no length prefix on the body itself —
     * the bytes:int field already specifies its length, and the body is
     * 4-byte-aligned TL data). */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_msg_container);
    tl_write_uint32(&w, (uint32_t)n_children);
    /* Use a monotonic msg_id/seqno pair per child. The parser's alignment
     * guard only cares that each body_len is a multiple of 4. */
    uint64_t base_msg_id = (uint64_t)time(NULL) << 32;
    for (size_t i = 0; i < n_children; ++i) {
        tl_write_uint64(&w, base_msg_id + ((uint64_t)i << 2));
        tl_write_uint32(&w, (uint32_t)(i * 2 + 1));
        tl_write_uint32(&w, (uint32_t)child_lens[i]);
        tl_write_raw(&w, children[i], child_lens[i]);
    }
    /* Send the container as the outer body — NOT wrapped in rpc_result. */
    queue_frame(w.data, w.len);
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

void mt_server_set_wrong_session_id_once(void) {
    g_srv.wrong_session_id_once_pending = 1;
}

/* ---- TEST-88 service-frame helpers ---------------------------------- */

/** Append a service-frame body (raw TL, CRC-prefixed) to the queue drained
 *  ahead of the next handler dispatch. Silently drops if the queue is full
 *  (tests that need more than MT_MAX_PENDING_SVC would need to raise the
 *  cap). Returns 1 on success, 0 on drop. */
static int svc_queue_push(const uint8_t *body, size_t body_len) {
    if (g_srv.pending_service_count >= MT_MAX_PENDING_SVC) return 0;
    uint8_t *copy = (uint8_t *)malloc(body_len);
    if (!copy) return 0;
    memcpy(copy, body, body_len);
    g_srv.pending_service_frames[g_srv.pending_service_count].bytes = copy;
    g_srv.pending_service_frames[g_srv.pending_service_count].len   = body_len;
    g_srv.pending_service_count++;
    return 1;
}

void mt_server_reply_bad_server_salt(uint64_t new_salt) {
    /* Reuse the existing one-shot path — the SVC_BAD_SALT branch is
     * sensitive to ordering (the client retries BEFORE reading any other
     * queued frame) so piping through set_bad_salt_once keeps the flow
     * identical to what rpc_send_encrypted observes on real hardware. */
    mt_server_set_bad_salt_once(new_salt);
}

void mt_server_reply_new_session_created(void) {
    /* new_session_created#9ec20908 first_msg_id:long unique_id:long
     *                              server_salt:long = NewSession */
    uint8_t body[4 + 8 + 8 + 8];
    uint32_t crc = CRC_new_session_created;
    for (int i = 0; i < 4; ++i) body[i] = (uint8_t)(crc >> (i * 8));
    /* first_msg_id — arbitrary recognisable pattern. */
    uint64_t first = 0xF111F111F111F111ULL;
    for (int i = 0; i < 8; ++i) body[4 + i]  = (uint8_t)(first >> (i * 8));
    /* unique_id. */
    uint64_t uniq = 0xABC0ABC0ABC0ABC0ULL;
    for (int i = 0; i < 8; ++i) body[12 + i] = (uint8_t)(uniq >> (i * 8));
    /* server_salt — the value the client must adopt. Keep this stable so
     * tests can assert against a constant. */
    uint64_t fresh = 0xCAFEF00DBAADC0DEULL;
    for (int i = 0; i < 8; ++i) body[20 + i] = (uint8_t)(fresh >> (i * 8));
    svc_queue_push(body, sizeof(body));
}

void mt_server_reply_msgs_ack(const uint64_t *ids, size_t n) {
    /* msgs_ack#62d6b459 msg_ids:Vector<long> */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_msgs_ack);
    tl_write_uint32(&w, CRC_vector);
    tl_write_uint32(&w, (uint32_t)n);
    for (size_t i = 0; i < n; ++i) {
        tl_write_uint64(&w, ids ? ids[i] : (uint64_t)(0xAC000000DEAD0000ULL + i));
    }
    svc_queue_push(w.data, w.len);
    tl_writer_free(&w);
}

void mt_server_reply_pong(uint64_t msg_id, uint64_t ping_id) {
    /* pong#347773c5 msg_id:long ping_id:long = Pong */
    uint8_t body[4 + 8 + 8];
    uint32_t crc = CRC_pong;
    for (int i = 0; i < 4; ++i) body[i] = (uint8_t)(crc >> (i * 8));
    for (int i = 0; i < 8; ++i) body[4 + i]  = (uint8_t)(msg_id  >> (i * 8));
    for (int i = 0; i < 8; ++i) body[12 + i] = (uint8_t)(ping_id >> (i * 8));
    svc_queue_push(body, sizeof(body));
}

void mt_server_reply_bad_msg_notification(uint64_t bad_id, int code) {
    /* bad_msg_notification#a7eff811 bad_msg_id:long bad_msg_seqno:int
     *                               error_code:int = BadMsgNotification */
    uint8_t body[4 + 8 + 4 + 4];
    uint32_t crc = CRC_bad_msg_notification;
    for (int i = 0; i < 4; ++i) body[i] = (uint8_t)(crc >> (i * 8));
    for (int i = 0; i < 8; ++i) body[4 + i]  = (uint8_t)(bad_id >> (i * 8));
    int32_t seqno = 0;
    for (int i = 0; i < 4; ++i) body[12 + i] = (uint8_t)(seqno >> (i * 8));
    int32_t ec = code;
    for (int i = 0; i < 4; ++i) body[16 + i] = (uint8_t)(ec >> (i * 8));
    svc_queue_push(body, sizeof(body));
}

void mt_server_stack_service_frames(size_t count) {
    /* Stack N msgs_ack frames. Each carries a distinct synthetic msg_id
     * so the client can log them individually (not that classify_service_frame
     * inspects the body — but giving them unique ids keeps the wire traffic
     * realistic). */
    for (size_t i = 0; i < count; ++i) {
        uint64_t id = 0xAC000000DEAD0000ULL + i;
        mt_server_reply_msgs_ack(&id, 1);
    }
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

    /* Drain any service frames queued by mt_server_reply_* helpers
     * (TEST-88). They land on the wire ahead of the real result so the
     * client's classify_service_frame loop observes them exactly once per
     * iteration. queue_frame wraps each body in its own encrypted envelope,
     * which the client treats as an independent frame. */
    for (size_t i = 0; i < g_srv.pending_service_count; ++i) {
        queue_frame(g_srv.pending_service_frames[i].bytes,
                    g_srv.pending_service_frames[i].len);
        free(g_srv.pending_service_frames[i].bytes);
        g_srv.pending_service_frames[i].bytes = NULL;
        g_srv.pending_service_frames[i].len   = 0;
    }
    g_srv.pending_service_count = 0;

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
    uint64_t effective_session_id = g_srv.session_id;
    if (g_srv.wrong_session_id_once_pending) {
        g_srv.wrong_session_id_once_pending = 0;
        effective_session_id ^= 0xFFFFFFFFFFFFFFFFULL;  /* flip all bits */
    }
    tl_write_uint64(&plain, effective_session_id);
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
