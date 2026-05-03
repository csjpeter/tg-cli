/**
 * @file mock_tel_server.c
 * @brief In-process Telegram server emulator — see mock_tel_server.h.
 */

#include "mock_tel_server.h"
#include "mock_socket.h"

#include "crypto.h"
#include "ige_aes.h"
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

/* TL CRCs for auth.sendCode / auth.signIn — local copies so the mock
 * server does not have to link against src/infrastructure/auth_session.h.
 * Kept aligned with CRC_auth_sendCode / CRC_auth_signIn in that header. */
#define CRC_auth_sendCode_local  0xa677244fU
#define CRC_auth_signIn_local    0x8d52a951U

/* TL CRCs for auth.exportAuthorization / auth.importAuthorization and
 * their reply constructors — local copies so the mock stays decoupled
 * from src/infrastructure/auth_transfer.c. Kept aligned with the
 * CRC_* macros in that file. */
#define CRC_auth_exportAuthorization_local        0xe5bfffcdU
#define CRC_auth_exportedAuthorization_local      0xb434e2b8U
#define CRC_auth_importAuthorization_local        0xa57a7dadU
#define CRC_auth_authorization_local              0x2ea2c0d4U
#define CRC_auth_authorizationSignUpRequired_local 0x44747e9aU
#define CRC_user_local                            0x3ff6ecb0U

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

    /* TEST-71 / US-20 cold-boot handshake state.
     * When handshake_mode != 0 the mock processes unencrypted frames
     * (auth_key_id == 0) alongside encrypted ones, emitting synthetic
     * resPQ / server_DH_params_ok envelopes so functional tests can
     * exercise src/infrastructure/mtproto_auth.c against real OpenSSL.
     * Mode 3 (TEST-72) completes the full DH exchange: RSA-PAD decrypt,
     * server_DH_params_ok, dh_gen_ok — enabling full auth_key derivation. */
    int         handshake_mode;          /* 0=off 1=resPQ-only 2=through step3 3=full DH */
    int         handshake_cold_boot_variant; /* MtColdBootMode value */
    int         handshake_req_pq_count;
    int         handshake_req_dh_count;
    int         handshake_set_client_dh_count; /* TEST-72 */

    /* TEST-72: DH state preserved between req_DH_params and set_client_DH_params. */
    uint8_t     hs_new_nonce[32];      /* extracted from client's RSA_PAD payload */
    uint8_t     hs_server_nonce[16];   /* echoed from resPQ */
    uint8_t     hs_nonce[16];          /* client nonce */
    uint8_t     hs_b[256];             /* server's DH secret (random) */
    uint8_t     hs_dh_prime[32];       /* safe prime for DH */
    uint8_t     hs_g_a[256];           /* g^a mod p (received from client) */
    size_t      hs_g_a_len;
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
static void handshake_on_req_pq_multi(const uint8_t *body, size_t body_len);
static void handshake_on_req_dh_params(const uint8_t *body, size_t body_len);
static void handshake_on_set_client_dh(const uint8_t *body, size_t body_len);
static void handshake_queue_unenc(const uint8_t *tl, size_t tl_len);

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

/* ---- TEST-86 PHONE/USER/NETWORK_MIGRATE helpers ----------------------
 *
 * Three internal int slots carry the target DC across handler dispatch.
 * Tests can reassign a new DC between calls by re-invoking the helper.
 * Using static slots (rather than malloc'd ctx) keeps the helpers
 * alloc-free and matches the rest of the mock-server setup style. */
static int g_phone_migrate_dc   = 0;
static int g_user_migrate_dc    = 0;
static int g_network_migrate_dc = 0;

static void on_phone_migrate(MtRpcContext *ctx) {
    int dc = g_phone_migrate_dc;
    char buf[64];
    snprintf(buf, sizeof(buf), "PHONE_MIGRATE_%d", dc);
    mt_server_reply_error(ctx, 303, buf);
}

static void on_user_migrate(MtRpcContext *ctx) {
    int dc = g_user_migrate_dc;
    char buf[64];
    snprintf(buf, sizeof(buf), "USER_MIGRATE_%d", dc);
    mt_server_reply_error(ctx, 303, buf);
}

static void on_network_migrate(MtRpcContext *ctx) {
    int dc = g_network_migrate_dc;
    char buf[64];
    snprintf(buf, sizeof(buf), "NETWORK_MIGRATE_%d", dc);
    mt_server_reply_error(ctx, 303, buf);
}

void mt_server_reply_phone_migrate(int dc_id) {
    g_phone_migrate_dc = dc_id;
    mt_server_expect(CRC_auth_sendCode_local, on_phone_migrate, NULL);
}

void mt_server_reply_user_migrate(int dc_id) {
    g_user_migrate_dc = dc_id;
    mt_server_expect(CRC_auth_signIn_local, on_user_migrate, NULL);
}

void mt_server_reply_network_migrate(int dc_id) {
    g_network_migrate_dc = dc_id;
    mt_server_expect(CRC_auth_sendCode_local, on_network_migrate, NULL);
}

/* ---- TEST-70 / US-19 auth.exportAuthorization / importAuthorization ----
 *
 * Static slots hold the export token (id + bytes) and an override flag
 * for the one-shot AUTH_KEY_INVALID case. Using statics keeps the helper
 * API alloc-free and mirrors how the PHONE/USER/NETWORK_MIGRATE helpers
 * store their target DC. */
#define MT_EXPORT_BYTES_MAX 1024
static int64_t g_export_id = 0;
static uint8_t g_export_bytes[MT_EXPORT_BYTES_MAX];
static size_t  g_export_bytes_len = 0;
static int     g_import_sign_up = 0;
static int     g_import_auth_key_invalid_pending = 0;

static void on_export_authorization(MtRpcContext *ctx) {
    /* auth.exportedAuthorization#b434e2b8 id:long bytes:bytes = auth.ExportedAuthorization */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_auth_exportedAuthorization_local);
    tl_write_int64 (&w, g_export_id);
    tl_write_bytes (&w, g_export_bytes, g_export_bytes_len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

static void on_import_authorization(MtRpcContext *ctx) {
    if (g_import_auth_key_invalid_pending) {
        /* Simulate a server-side token expiry race: the token we just
         * issued has gone stale before the client imported it. */
        g_import_auth_key_invalid_pending = 0;
        mt_server_reply_error(ctx, 401, "AUTH_KEY_INVALID");
        return;
    }
    TlWriter w;
    tl_writer_init(&w);
    if (g_import_sign_up) {
        /* auth.authorizationSignUpRequired#44747e9a has flags:#
         * terms_of_service:flags.0?help.TermsOfService = auth.Authorization.
         * Emit flags=0 (no TOS attached) — the client only cares about the
         * constructor CRC. */
        tl_write_uint32(&w, CRC_auth_authorizationSignUpRequired_local);
        tl_write_uint32(&w, 0);
    } else {
        /* auth.authorization#2ea2c0d4 flags:# ... user:User = auth.Authorization.
         * Emit the minimal shape accepted by the client's parser:
         * flags=0 + a user#3ff6ecb0 stub with flags=0 and id=101. */
        tl_write_uint32(&w, CRC_auth_authorization_local);
        tl_write_uint32(&w, 0);                       /* outer flags */
        tl_write_uint32(&w, CRC_user_local);          /* user constructor */
        tl_write_uint32(&w, 0);                       /* user.flags */
        tl_write_int64 (&w, 101LL);                   /* user.id */
    }
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

void mt_server_reply_export_authorization(int64_t id,
                                           const uint8_t *bytes, size_t len) {
    if (!bytes || len == 0 || len > MT_EXPORT_BYTES_MAX) return;
    g_export_id = id;
    memcpy(g_export_bytes, bytes, len);
    g_export_bytes_len = len;
    mt_server_expect(CRC_auth_exportAuthorization_local,
                     on_export_authorization, NULL);
}

void mt_server_reply_import_authorization(int sign_up) {
    g_import_sign_up = sign_up ? 1 : 0;
    mt_server_expect(CRC_auth_importAuthorization_local,
                     on_import_authorization, NULL);
}

void mt_server_reply_import_authorization_auth_key_invalid_once(void) {
    g_import_auth_key_invalid_pending = 1;
    /* Next dispatch after the AUTH_KEY_INVALID one-shot falls through to
     * the happy auth.authorization path (sign_up=0) so callers that want
     * a full reject→retry cycle do not need two helper calls. */
    g_import_sign_up = 0;
    mt_server_expect(CRC_auth_importAuthorization_local,
                     on_import_authorization, NULL);
}

/* ---- TEST-71 / US-20 cold-boot DH handshake helpers ----
 *
 * The mock cannot decrypt the client's RSA_PAD(inner_data) because the
 * canonical Telegram RSA private key is not available. These helpers
 * therefore cover exhaustively only the paths reachable without that
 * private key: resPQ generation (step 1), plus a synthetic
 * server_DH_params_ok whose AES-IGE-wrapped payload decrypts to random
 * bytes and is rejected by the client's inner CRC check (step 3 error
 * path). Tests assert handshake start, negative variants, and
 * no-persistence-on-failure. */

/* TEST-72: The functional test runner links tests/mocks/telegram_server_key.c
 * which defines TELEGRAM_RSA_FINGERPRINT = 0x8671de275f1cabc5ULL (test-only
 * 2048-bit key pair). This constant must match that value so resPQ lists a
 * fingerprint the production client recognises during handshake tests.
 * NEVER use this fingerprint in production. */
#define COLD_BOOT_FP_OK           0x8671de275f1cabc5ULL
#define COLD_BOOT_FP_BAD          0xDEADBEEFCAFEBABEULL
#define CRC_resPQ                 0x05162463U
#define CRC_req_pq_multi          0xbe7e8ef1U
#define CRC_req_DH_params         0xd712e4beU
#define CRC_server_DH_params_ok   0xd0e8075cU
#define CRC_server_DH_inner_data  0xb5890dbaU
#define CRC_set_client_DH_params  0xf5045f1fU
#define CRC_dh_gen_ok             0x3bcbf734U

void mt_server_simulate_cold_boot(MtColdBootMode mode) {
    /* A cold-boot session has no persisted auth_key_id. Clear the seeded
     * flag so on_client_sent's guard lets unencrypted frames through, but
     * keep initialised state + mock_socket hook registered via reset(). */
    g_srv.handshake_mode = 1;
    g_srv.handshake_cold_boot_variant = (int)mode;
    g_srv.handshake_req_pq_count = 0;
    g_srv.handshake_req_dh_count = 0;
    /* Ensure auth_key_id is 0 so the parser takes the unencrypted branch. */
    g_srv.auth_key_id = 0;
    memset(g_srv.auth_key, 0, MT_SERVER_AUTH_KEY_SIZE);
    /* Allow on_client_sent to parse — the "seeded" label in this mock
     * means "ready to parse wire traffic", not "holds a post-handshake
     * auth key". Treat handshake_mode as an alternate seed state. */
    g_srv.seeded = 1;
}

void mt_server_simulate_cold_boot_through_step3(void) {
    /* Same as cold_boot(OK) but the mock also replies to req_DH_params
     * with a synthetic server_DH_params_ok. */
    if (g_srv.handshake_mode == 0) {
        mt_server_simulate_cold_boot(MT_COLD_BOOT_OK);
    }
    g_srv.handshake_mode = 2;
}

void mt_server_simulate_full_dh_handshake(void) {
    /* TEST-72: Full DH handshake — the mock RSA-PAD-decrypts the client's
     * req_DH_params payload (using the test RSA private key), generates
     * valid server_DH_inner_data, and handles set_client_DH_params to
     * return dh_gen_ok. On success the session auth_key is set and the
     * session is persisted. NEVER call this outside functional tests. */
    mt_server_simulate_cold_boot(MT_COLD_BOOT_OK);
    g_srv.handshake_mode = 3;
    g_srv.handshake_set_client_dh_count = 0;
}

int mt_server_handshake_req_pq_count(void) {
    return g_srv.handshake_req_pq_count;
}

int mt_server_handshake_req_dh_count(void) {
    return g_srv.handshake_req_dh_count;
}

int mt_server_handshake_set_client_dh_count(void) {
    return g_srv.handshake_set_client_dh_count;
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

        /* TEST-71: in cold-boot handshake mode the server's auth_key_id is
         * 0 (no session yet) and the client's frame also carries
         * auth_key_id = 0, so the equality check below would incorrectly
         * route the frame into the encrypted branch. Short-circuit here
         * and dispatch to the synthetic handshake responders instead. */
        if (g_srv.handshake_mode && key_id == 0 && payload_len >= 24) {
            uint32_t raw_crc = (uint32_t)frame[20]
                             | ((uint32_t)frame[21] << 8)
                             | ((uint32_t)frame[22] << 16)
                             | ((uint32_t)frame[23] << 24);
            size_t slot = g_srv.crc_ring_count % MT_CRC_RING_SIZE;
            g_srv.crc_ring[slot] = raw_crc;
            g_srv.crc_ring_count++;

            const uint8_t *tl_body = frame + 20;
            size_t tl_body_len = payload_len - 20;
            if (raw_crc == CRC_req_pq_multi) {
                g_srv.handshake_req_pq_count++;
                handshake_on_req_pq_multi(tl_body, tl_body_len);
            } else if (raw_crc == CRC_req_DH_params
                       && g_srv.handshake_mode >= 2) {
                g_srv.handshake_req_dh_count++;
                handshake_on_req_dh_params(tl_body, tl_body_len);
            } else if (raw_crc == CRC_req_DH_params) {
                /* Count it but do not reply — tests can observe the
                 * counter to prove the client reached step 2. */
                g_srv.handshake_req_dh_count++;
            } else if (raw_crc == CRC_set_client_DH_params
                       && g_srv.handshake_mode == 3) {
                /* TEST-72: full DH — compute auth_key from client's g_b,
                 * verify key_hash, send dh_gen_ok, persist session. */
                g_srv.handshake_set_client_dh_count++;
                handshake_on_set_client_dh(tl_body, tl_body_len);
            }
            continue;
        }

        if (key_id != g_srv.auth_key_id) {
            /* Unencrypted handshake frame: auth_key_id == 0.
             * Record the leading CRC for mt_server_request_crc_count().
             * Unencrypted layout: key_id(8) + msg_id(8) + msg_len(4) + body */
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

/* ---- TEST-71 / US-20 handshake unencrypted-frame handlers ---- */

/** Queue an unencrypted (auth_key_id = 0) server → client frame with the
 *  handshake response TL. Mirrors rpc_send_unencrypted on the client side
 *  so the client's rpc_recv_unencrypted parser picks it up. */
static void handshake_queue_unenc(const uint8_t *tl, size_t tl_len) {
    /* Wire: auth_key_id(8) + msg_id(8) + len(4) + body */
    TlWriter w; tl_writer_init(&w);
    tl_write_uint64(&w, 0);                       /* auth_key_id */
    tl_write_uint64(&w, make_server_msg_id());    /* server msg_id */
    tl_write_uint32(&w, (uint32_t)tl_len);
    tl_write_raw(&w, tl, tl_len);

    size_t units = w.len / 4;
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
    mock_socket_append_response(w.data, w.len);
    tl_writer_free(&w);
}

/** Handle an incoming req_pq_multi frame, emit a resPQ per the current
 *  cold-boot variant. Assumes @p body points at the CRC-prefixed TL body
 *  (CRC already consumed by the caller would break layout — we take the
 *  whole 4+16 body here). */
static void handshake_on_req_pq_multi(const uint8_t *body, size_t body_len) {
    if (body_len < 4 + 16) return;
    /* body = CRC(4) nonce(16) */
    uint8_t client_nonce[16];
    memcpy(client_nonce, body + 4, 16);

    /* Echo (possibly tampered) nonce back. */
    uint8_t echo_nonce[16];
    memcpy(echo_nonce, client_nonce, 16);
    if (g_srv.handshake_cold_boot_variant == MT_COLD_BOOT_NONCE_TAMPER) {
        for (int i = 0; i < 16; ++i) echo_nonce[i] ^= 0xFF;
    }

    /* Deterministic server_nonce for reproducibility. */
    uint8_t server_nonce[16];
    memset(server_nonce, 0xBB, 16);

    /* PQ: default 21 (= 3 * 7 — tiny so Pollard's rho finishes instantly).
     * MT_COLD_BOOT_BAD_PQ uses a 64-bit prime so pq_factorize fails. */
    uint64_t pq_val = 21ULL;
    if (g_srv.handshake_cold_boot_variant == MT_COLD_BOOT_BAD_PQ) {
        /* 2^64 - 59 is a prime. Small enough to survive Pollard's rho
         * 20-c sweep without factoring. */
        pq_val = 0xFFFFFFFFFFFFFFC5ULL;
    }
    /* Encode pq as big-endian minimum-length bytes. */
    uint8_t pq_be[8];
    size_t pq_be_len = 0;
    for (int i = 7; i >= 0; --i) {
        uint8_t byte = (uint8_t)((pq_val >> (i * 8)) & 0xFFu);
        if (pq_be_len > 0 || byte != 0 || i == 0) {
            pq_be[pq_be_len++] = byte;
        }
    }

    uint64_t fingerprint = COLD_BOOT_FP_OK;
    if (g_srv.handshake_cold_boot_variant == MT_COLD_BOOT_BAD_FINGERPRINT) {
        fingerprint = COLD_BOOT_FP_BAD;
    }

    uint32_t constructor = CRC_resPQ;
    if (g_srv.handshake_cold_boot_variant == MT_COLD_BOOT_WRONG_CONSTRUCTOR) {
        constructor = 0xDEADBEEFU;
    }

    TlWriter tl; tl_writer_init(&tl);
    tl_write_uint32(&tl, constructor);
    tl_write_int128(&tl, echo_nonce);
    tl_write_int128(&tl, server_nonce);
    tl_write_bytes(&tl, pq_be, pq_be_len);
    tl_write_uint32(&tl, CRC_vector);
    tl_write_uint32(&tl, 1);                    /* one fingerprint entry */
    tl_write_uint64(&tl, fingerprint);
    handshake_queue_unenc(tl.data, tl.len);
    tl_writer_free(&tl);
}

/* ---- TEST-72: RSA-PAD decrypt (inverse of rsa_pad_encrypt in mtproto_auth.c) ----
 *
 * Reverses the RSA_PAD scheme used by the client to encrypt p_q_inner_data_dc.
 * Returns 0 on success and writes the decrypted payload into @p plain_out[192].
 *
 * TEST_ONLY — only called from handshake_on_req_dh_params in mode 3. */
static int rsa_pad_decrypt(const uint8_t *ciphertext, size_t cipher_len,
                           uint8_t *plain_out  /* must be >= 192 bytes */) {
    if (cipher_len != 256) return -1;

    /* TEST_ONLY private key — matches tests/mocks/telegram_server_key.c pubkey. */
    static const char TEST_PRIVATE_KEY_PEM[] =
        "-----BEGIN PRIVATE KEY-----\n"
        "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCbG/j8RdvTAAWv\n"
        "870ayFCbJI73fEEB43/l/NnocYeAh9L/Zcv9HwYwFOXms9o2ccvp+e/4GE55vUzY\n"
        "8XrM1h6dtClGYNvRNpvcthfmVrpGGIjKH2b3snip4ajtUOcZIyTynZo1vMG5uqCx\n"
        "YaXWyhBwMPJQ87Gw5WbcaKNJWg3jZ1GI0g+tJUAqXpfPwF3KjKzIZwa/rbnJ9ick\n"
        "OX12OaG1c2enW1+sv1K1qz6CcxmbCyRuD+xUKVTLHc5ykmcYDJ12CLES8DdfbPOt\n"
        "UGAP082CQOptfpPm5fvnU6c53suIItnddjiT74+hyQBXQpvWXKIZzFEdgxJ2QUzp\n"
        "TlRwqVijAgMBAAECggEAJll6rIjrKlaRkWjOgw4u28Tksize98QTTb4/9DgJnA44\n"
        "7VtyXYFrqryoAOvL0nU9SPq6SZlc4b2bf/HofjecdzphkByHjMkXLTE6ZIFh6c3M\n"
        "GEk+UJSoP7xi41YC5VSqoG+1/n5OWYjajTDK63mnKc34Q2qVLtrxHSKj6JFi6Kuy\n"
        "uO1+lXUiGS5vvlPllAoDTIFn4e4PHChHeVNiwzBe8HNgVWc9JasL7IiS37gsfuZC\n"
        "l4QlW3IkhCMkb5fj7xV/xcwQ4SuYWcNkrfuX7Fu3g4Hpeir3xJlJMxh2iqJnARvM\n"
        "nLy9gY04ejqdxI9+VopQyXafe0tP/veuqO7RzR3x+QKBgQDQvd9drWmvWM9g7Lrj\n"
        "jEr4oo00b5/cOQd12VJaxhWUdfzyDxsygGPeuP3/7QxgwxB+VmnsS2xfNvv4hDdd\n"
        "vL6DhFPicTV5Xv25U52QaxNefc5XSSzY68XMFtUyWjzD+35GIUO27TnDfQhFBkbS\n"
        "RFDg3uJ1KbcI05nM+DSQJZPaqQKBgQC+ObQxrgZBPd6K6vVM/uaoA45Htc8UVIS/\n"
        "+eU69k1lVp6BZm1ebaTOf6CTOO/AcyM4jZu7lVFwfanVMVSfJ+Nbd1BEXtg1BKou\n"
        "2cLlYkJX6mVmCs39sQDscvoO67RMhR4jshCE0nxaAxFatdEfpq9ZTrQIXWKnQhRq\n"
        "nHsyIyHUawKBgQCwTf5vx70AvfkB+1BqSp8z2096X2FdBsn3TqORSccGSpVm+T1W\n"
        "bTxs7ECUPWn7/CVdH619R8LztKQjJcEBqh4bRNP46PdqWMHiGu51AQst/wIdlQ+M\n"
        "865vj0VorvCt8yeXIhdoVHs6Ust+SSveApdxJq+Ml7whd19q0KTMrwBvaQKBgC8C\n"
        "i6mLXDhbVdf24NA6Xj4/QrYuFBLuIDBhTWkY3V+h3GIWMgkYB5aQq9o2Q+nHini7\n"
        "ZjUhXZLzOzlYi5UZgnJkNg3vcncHxBb38dZGRib74jspiGadi6DjeTCex1vxudUQ\n"
        "eEyax+hmwa8tJ5Uu2D612IAItAypo+oE6d0mGYIpAoGAS2P6e2iOW2HG3CA2kiLp\n"
        "xw/guEi8BTTljxMSUEqS5YhDI2uVe2QEYlvgDG7zYMl2UzoadZcK1VVa01WM2LDC\n"
        "wThzA+NhfHS4ukPuuOkJUVwGAJGUG/KcIypTTYz4RPj6BpALLXstF+Qf8F3aqDvL\n"
        "bAd3PHlfEWOhiWzBj/dVjUY=\n"
        "-----END PRIVATE KEY-----\n";

    CryptoRsaKey *priv = crypto_rsa_load_private(TEST_PRIVATE_KEY_PEM);
    if (!priv) {
        fprintf(stderr, "mock: rsa_pad_decrypt: failed to load private key\n");
        return -1;
    }

    /* Step 1: RSA decrypt (NO_PADDING) → rsa_output[256] */
    uint8_t rsa_output[256];
    size_t rsa_out_len = sizeof(rsa_output);
    if (crypto_rsa_private_decrypt(priv, ciphertext, cipher_len,
                                   rsa_output, &rsa_out_len) != 0) {
        crypto_rsa_free(priv);
        fprintf(stderr, "mock: rsa_pad_decrypt: RSA decrypt failed\n");
        return -1;
    }
    crypto_rsa_free(priv);

    /* Step 2: Split into temp_key_xor(32) + aes_encrypted(224) */
    const uint8_t *temp_key_xor = rsa_output;
    const uint8_t *aes_encrypted = rsa_output + 32;

    /* Step 3: SHA256(aes_encrypted) → aes_hash */
    uint8_t aes_hash[32];
    crypto_sha256(aes_encrypted, 224, aes_hash);

    /* Step 4: temp_key = temp_key_xor XOR aes_hash */
    uint8_t temp_key[32];
    for (int i = 0; i < 32; i++) temp_key[i] = temp_key_xor[i] ^ aes_hash[i];

    /* Step 5: AES-IGE decrypt(aes_encrypted, key=temp_key, IV=zeros) → data_with_hash */
    uint8_t zero_iv[32];
    memset(zero_iv, 0, 32);
    uint8_t data_with_hash[224];
    aes_ige_decrypt(aes_encrypted, 224, temp_key, zero_iv, data_with_hash);

    /* Step 6: data_with_hash = data_pad_reversed(192) + hash(32).
     * Un-reverse first to get data_with_padding. */
    for (int i = 0; i < 192; i++) plain_out[i] = data_with_hash[191 - i];

    /* Step 7: Verify SHA256(temp_key + data_with_padding) == hash.
     * Per spec: hash is over the NON-reversed block (data_with_padding). */
    uint8_t verify_buf[32 + 192];
    memcpy(verify_buf, temp_key, 32);
    memcpy(verify_buf + 32, plain_out, 192);
    uint8_t expected_hash[32];
    crypto_sha256(verify_buf, sizeof(verify_buf), expected_hash);
    if (memcmp(expected_hash, data_with_hash + 192, 32) != 0) {
        fprintf(stderr, "mock: rsa_pad_decrypt: hash mismatch\n");
        return -1;
    }

    return 0;
}

/** Handle an incoming req_DH_params frame.
 *
 * Mode 2 (original): emits server_DH_params_ok with random encrypted_answer
 * (garbage). The client's AES-IGE decrypt yields garbage, the inner CRC check
 * fails, and auth_step_parse_dh returns -1 — the negative path.
 *
 * Mode 3 (TEST-72): RSA-PAD-decrypts the client's payload to extract new_nonce,
 * generates a valid server_DH_inner_data (g=2, safe prime, g_a=g^b mod p),
 * AES-IGE-encrypts it with the derived temp key, and sends server_DH_params_ok.
 * Stores DH state for subsequent set_client_DH_params handling. */
static void handshake_on_req_dh_params(const uint8_t *body, size_t body_len) {
    if (body_len < 4 + 16 + 16) return;
    /* body = CRC(4) nonce(16) server_nonce(16) p:bytes q:bytes fp:long encrypted_data:bytes */
    uint8_t nonce[16], server_nonce[16];
    memcpy(nonce,        body + 4,       16);
    memcpy(server_nonce, body + 4 + 16,  16);

    if (g_srv.handshake_mode < 3) {
        /* Original mode 2 path: random garbage encrypted_answer. */
        uint8_t enc_answer[32];
        crypto_rand_bytes(enc_answer, sizeof(enc_answer));
        TlWriter tl; tl_writer_init(&tl);
        tl_write_uint32(&tl, CRC_server_DH_params_ok);
        tl_write_int128(&tl, nonce);
        tl_write_int128(&tl, server_nonce);
        tl_write_bytes(&tl, enc_answer, sizeof(enc_answer));
        handshake_queue_unenc(tl.data, tl.len);
        tl_writer_free(&tl);
        return;
    }

    /* ---- Mode 3: full DH handshake ---- */

    /* Parse the req_DH_params body to find the encrypted_data.
     * Layout: CRC(4) nonce(16) server_nonce(16) p:bytes q:bytes fp:int64 encrypted_data:bytes */
    TlReader r = tl_reader_init(body, body_len);
    tl_read_uint32(&r);            /* CRC */
    uint8_t _nonce[16]; tl_read_int128(&r, _nonce);
    uint8_t _snonce[16]; tl_read_int128(&r, _snonce);
    size_t p_len = 0, q_len = 0, enc_len = 0;
    uint8_t *p_bytes = tl_read_bytes(&r, &p_len);
    free(p_bytes);
    uint8_t *q_bytes = tl_read_bytes(&r, &q_len);
    free(q_bytes);
    tl_read_uint64(&r);            /* fingerprint */
    uint8_t *enc_data = tl_read_bytes(&r, &enc_len);
    if (!enc_data || enc_len != 256) {
        free(enc_data);
        fprintf(stderr, "mock: full DH: unexpected enc_data size %zu\n", enc_len);
        return;
    }

    /* RSA-PAD decrypt to get padded p_q_inner_data_dc (192 bytes) */
    uint8_t inner_plain[192];
    if (rsa_pad_decrypt(enc_data, enc_len, inner_plain) != 0) {
        free(enc_data);
        fprintf(stderr, "mock: full DH: RSA_PAD decrypt failed\n");
        return;
    }
    free(enc_data);

    /* Parse p_q_inner_data_dc from inner_plain.
     * RSA_PAD output: data[...] + padding (no hash prefix — per MTProto spec).
     * TL: CRC(4) pq:bytes p:bytes q:bytes nonce(16) server_nonce(16) new_nonce(32) dc:int */
    TlReader ir = tl_reader_init(inner_plain, sizeof(inner_plain));
    uint32_t inner_crc = tl_read_uint32(&ir);
    if (inner_crc != 0xa9f55f95U) {  /* CRC_p_q_inner_data_dc */
        fprintf(stderr, "mock: full DH: unexpected inner CRC 0x%08x\n", inner_crc);
        return;
    }
    /* Skip pq, p, q bytes */
    size_t tmp_len = 0;
    uint8_t *tmp = tl_read_bytes(&ir, &tmp_len); free(tmp);  /* pq */
    tmp = tl_read_bytes(&ir, &tmp_len); free(tmp);             /* p */
    tmp = tl_read_bytes(&ir, &tmp_len); free(tmp);             /* q */
    /* Read nonce, server_nonce, new_nonce */
    uint8_t extracted_nonce[16], extracted_snonce[16], new_nonce[32];
    tl_read_int128(&ir, extracted_nonce);
    tl_read_int128(&ir, extracted_snonce);
    tl_read_int256(&ir, new_nonce);

    /* Save DH state for set_client_DH_params */
    memcpy(g_srv.hs_new_nonce,    new_nonce,        32);
    memcpy(g_srv.hs_nonce,        extracted_nonce,  16);
    memcpy(g_srv.hs_server_nonce, extracted_snonce, 16);

    /* Use a fixed 256-bit safe prime for DH (TEST_ONLY).
     * This prime was generated with openssl: BN_generate_prime_ex(p,256,1,...).
     * g=2 is a generator for this group. */
    static const uint8_t TEST_DH_PRIME[32] = {
        0xfa, 0xd0, 0x8e, 0x08, 0xa4, 0x4d, 0x25, 0xaa,
        0x45, 0x2b, 0xda, 0x58, 0x62, 0xac, 0xc4, 0xb2,
        0x76, 0x23, 0xd3, 0x30, 0x4d, 0xd0, 0x9d, 0x64,
        0xc1, 0xdd, 0xc0, 0xfb, 0x35, 0x09, 0x40, 0xdb
    };
    memcpy(g_srv.hs_dh_prime, TEST_DH_PRIME, 32);

    /* Generate server-side DH secret b (256 bytes) and compute g_b = 2^b mod p */
    crypto_rand_bytes(g_srv.hs_b, 256);

    uint8_t g_be[1] = { 0x02 };  /* g = 2 as 1-byte big-endian */
    uint8_t g_b[32];
    size_t g_b_len = sizeof(g_b);
    CryptoBnCtx *bn_ctx = crypto_bn_ctx_new();
    if (!bn_ctx) { fprintf(stderr, "mock: full DH: BN ctx alloc failed\n"); return; }
    if (crypto_bn_mod_exp(g_b, &g_b_len, g_be, 1,
                          g_srv.hs_b, 256,
                          TEST_DH_PRIME, 32, bn_ctx) != 0) {
        crypto_bn_ctx_free(bn_ctx);
        fprintf(stderr, "mock: full DH: g_b computation failed\n");
        return;
    }
    crypto_bn_ctx_free(bn_ctx);

    /* Derive temp AES key/IV from new_nonce + server_nonce (same as production) */
    uint8_t tmp_aes_key[32], tmp_aes_iv[32];
    {
        uint8_t buf[64];
        uint8_t sha1_a[20], sha1_b[20], sha1_c[20];

        memcpy(buf, new_nonce, 32); memcpy(buf + 32, extracted_snonce, 16);
        crypto_sha1(buf, 48, sha1_a);
        memcpy(buf, extracted_snonce, 16); memcpy(buf + 16, new_nonce, 32);
        crypto_sha1(buf, 48, sha1_b);
        memcpy(buf, new_nonce, 32); memcpy(buf + 32, new_nonce, 32);
        crypto_sha1(buf, 64, sha1_c);

        memcpy(tmp_aes_key,      sha1_a, 20);
        memcpy(tmp_aes_key + 20, sha1_b, 12);
        memcpy(tmp_aes_iv,       sha1_b + 12, 8);
        memcpy(tmp_aes_iv + 8,   sha1_c, 20);
        memcpy(tmp_aes_iv + 28,  new_nonce, 4);
    }

    /* Build server_DH_inner_data TL */
    TlWriter inner; tl_writer_init(&inner);
    tl_write_uint32(&inner, CRC_server_DH_inner_data);
    tl_write_int128(&inner, extracted_nonce);
    tl_write_int128(&inner, extracted_snonce);
    tl_write_int32(&inner, 2);                     /* g = 2 */
    tl_write_bytes(&inner, TEST_DH_PRIME, 32);     /* dh_prime */
    tl_write_bytes(&inner, g_b, g_b_len);          /* g_a (server's g^b) */
    tl_write_int32(&inner, (int32_t)time(NULL));   /* server_time */

    /* Prepend SHA1 + pad to 16-byte boundary */
    uint8_t sha1_inner[20];
    crypto_sha1(inner.data, inner.len, sha1_inner);
    size_t raw_len = 20 + inner.len;
    size_t padded_len = (raw_len + 15) & ~(size_t)15;
    uint8_t *padded = (uint8_t *)calloc(1, padded_len);
    if (!padded) { tl_writer_free(&inner); return; }
    memcpy(padded, sha1_inner, 20);
    memcpy(padded + 20, inner.data, inner.len);
    if (padded_len > raw_len) {
        crypto_rand_bytes(padded + raw_len, padded_len - raw_len);
    }
    tl_writer_free(&inner);

    /* AES-IGE encrypt */
    uint8_t *enc_answer = (uint8_t *)malloc(padded_len);
    if (!enc_answer) { free(padded); return; }
    aes_ige_encrypt(padded, padded_len, tmp_aes_key, tmp_aes_iv, enc_answer);
    free(padded);

    /* Send server_DH_params_ok */
    TlWriter tl; tl_writer_init(&tl);
    tl_write_uint32(&tl, CRC_server_DH_params_ok);
    tl_write_int128(&tl, extracted_nonce);
    tl_write_int128(&tl, extracted_snonce);
    tl_write_bytes(&tl, enc_answer, padded_len);
    handshake_queue_unenc(tl.data, tl.len);
    tl_writer_free(&tl);
    free(enc_answer);
}

/** TEST-72: Handle set_client_DH_params, compute auth_key = g_b^a mod p,
 *  verify key_hash, send dh_gen_ok, and persist the session. */
static void handshake_on_set_client_dh(const uint8_t *body, size_t body_len) {
    if (body_len < 4 + 16 + 16) return;

    /* Derive temp AES key/IV from stored new_nonce + server_nonce */
    uint8_t tmp_aes_key[32], tmp_aes_iv[32];
    {
        uint8_t buf[64];
        uint8_t sha1_a[20], sha1_b[20], sha1_c[20];

        memcpy(buf, g_srv.hs_new_nonce, 32);
        memcpy(buf + 32, g_srv.hs_server_nonce, 16);
        crypto_sha1(buf, 48, sha1_a);

        memcpy(buf, g_srv.hs_server_nonce, 16);
        memcpy(buf + 16, g_srv.hs_new_nonce, 32);
        crypto_sha1(buf, 48, sha1_b);

        memcpy(buf, g_srv.hs_new_nonce, 32);
        memcpy(buf + 32, g_srv.hs_new_nonce, 32);
        crypto_sha1(buf, 64, sha1_c);

        memcpy(tmp_aes_key,      sha1_a, 20);
        memcpy(tmp_aes_key + 20, sha1_b, 12);
        memcpy(tmp_aes_iv,       sha1_b + 12, 8);
        memcpy(tmp_aes_iv + 8,   sha1_c, 20);
        memcpy(tmp_aes_iv + 28,  g_srv.hs_new_nonce, 4);
    }

    /* Parse set_client_DH_params body:
     * CRC(4) nonce(16) server_nonce(16) encrypted_data:bytes */
    TlReader r = tl_reader_init(body, body_len);
    tl_read_uint32(&r);              /* CRC */
    uint8_t _nonce[16]; tl_read_int128(&r, _nonce);
    uint8_t _snonce[16]; tl_read_int128(&r, _snonce);
    size_t enc_len = 0;
    uint8_t *enc_data = tl_read_bytes(&r, &enc_len);
    if (!enc_data || enc_len == 0 || enc_len % 16 != 0) {
        free(enc_data);
        fprintf(stderr, "mock: set_client_DH: bad encrypted_data len %zu\n", enc_len);
        return;
    }

    /* AES-IGE decrypt encrypted_data */
    uint8_t *plain = (uint8_t *)malloc(enc_len);
    if (!plain) { free(enc_data); return; }
    aes_ige_decrypt(enc_data, enc_len, tmp_aes_key, tmp_aes_iv, plain);
    free(enc_data);

    /* Parse client_DH_inner_data: SHA1(20) + CRC(4) nonce(16) server_nonce(16)
     *                               retry_id:int64 g_b:bytes */
    if (enc_len < 20 + 4 + 16 + 16 + 8) {
        free(plain);
        fprintf(stderr, "mock: set_client_DH: plaintext too short\n");
        return;
    }
    TlReader ir = tl_reader_init(plain + 20, enc_len - 20);
    uint32_t crc = tl_read_uint32(&ir);
    if (crc != 0x6643b654U) {  /* CRC_client_DH_inner_data */
        free(plain);
        fprintf(stderr, "mock: set_client_DH: wrong CRC 0x%08x\n", crc);
        return;
    }
    uint8_t cli_nonce[16]; tl_read_int128(&ir, cli_nonce);
    uint8_t cli_snonce[16]; tl_read_int128(&ir, cli_snonce);
    tl_read_uint64(&ir);  /* retry_id */
    size_t g_b_len = 0;
    uint8_t *g_b = tl_read_bytes(&ir, &g_b_len);  /* client's g^client_b mod p */
    if (!g_b || g_b_len == 0) {
        free(g_b); free(plain);
        fprintf(stderr, "mock: set_client_DH: failed to read g_b\n");
        return;
    }
    free(plain);

    /* Save g_a (client's g^client_b mod p) for auth_key computation */
    if (g_b_len > sizeof(g_srv.hs_g_a)) {
        free(g_b);
        fprintf(stderr, "mock: set_client_DH: g_b too large (%zu)\n", g_b_len);
        return;
    }
    memcpy(g_srv.hs_g_a, g_b, g_b_len);
    g_srv.hs_g_a_len = g_b_len;
    free(g_b);

    /* Compute auth_key = g_b^server_b mod dh_prime */
    uint8_t auth_key[256];
    size_t ak_len = sizeof(auth_key);
    CryptoBnCtx *bn_ctx = crypto_bn_ctx_new();
    if (!bn_ctx) { fprintf(stderr, "mock: set_client_DH: BN ctx alloc\n"); return; }
    if (crypto_bn_mod_exp(auth_key, &ak_len,
                          g_srv.hs_g_a, g_srv.hs_g_a_len,
                          g_srv.hs_b, 256,
                          g_srv.hs_dh_prime, 32, bn_ctx) != 0) {
        crypto_bn_ctx_free(bn_ctx);
        fprintf(stderr, "mock: set_client_DH: auth_key exp failed\n");
        return;
    }
    crypto_bn_ctx_free(bn_ctx);

    /* Pad auth_key to 256 bytes */
    uint8_t auth_key_padded[256];
    memset(auth_key_padded, 0, 256);
    if (ak_len <= 256) {
        memcpy(auth_key_padded + (256 - ak_len), auth_key, ak_len);
    }

    /* Compute new_nonce_hash1 = last 16 bytes of SHA1(new_nonce||0x01||ak_aux_hash[8])
     * where ak_aux_hash = SHA1(auth_key)[0:8] */
    uint8_t ak_full_hash[20];
    crypto_sha1(auth_key_padded, 256, ak_full_hash);

    uint8_t nnh_input[32 + 1 + 8];
    memcpy(nnh_input,      g_srv.hs_new_nonce, 32);
    nnh_input[32] = 0x01;
    memcpy(nnh_input + 33, ak_full_hash, 8);

    uint8_t nnh_full[20];
    crypto_sha1(nnh_input, sizeof(nnh_input), nnh_full);
    /* last 16 bytes = nnh_full[4:20] */
    uint8_t new_nonce_hash1[16];
    memcpy(new_nonce_hash1, nnh_full + 4, 16);

    /* Save auth_key to mock server state so encrypted frames work after handshake */
    memcpy(g_srv.auth_key, auth_key_padded, 256);
    g_srv.auth_key_id  = derive_auth_key_id(auth_key_padded);
    /* salt = new_nonce[0:8] XOR server_nonce[0:8] */
    g_srv.server_salt = 0;
    for (int i = 0; i < 8; i++) {
        ((uint8_t *)&g_srv.server_salt)[i] =
            g_srv.hs_new_nonce[i] ^ g_srv.hs_server_nonce[i];
    }
    g_srv.session_id = 0xAABBCCDD11223344ULL;

    /* Persist session so the client can verify it was written */
    MtProtoSession sess;
    mtproto_session_init(&sess);
    mtproto_session_set_auth_key(&sess, auth_key_padded);
    mtproto_session_set_salt(&sess, g_srv.server_salt);
    sess.session_id = g_srv.session_id;
    session_store_save(&sess, 2);  /* dc_id=2 matches the test */

    /* Send dh_gen_ok */
    TlWriter tl; tl_writer_init(&tl);
    tl_write_uint32(&tl, CRC_dh_gen_ok);
    tl_write_int128(&tl, g_srv.hs_nonce);
    tl_write_int128(&tl, g_srv.hs_server_nonce);
    tl_write_int128(&tl, new_nonce_hash1);
    handshake_queue_unenc(tl.data, tl.len);
    tl_writer_free(&tl);
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
