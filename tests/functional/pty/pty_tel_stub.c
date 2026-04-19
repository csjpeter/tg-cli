/**
 * @file pty_tel_stub.c
 * @brief Minimal TCP MTProto stub server implementation — see pty_tel_stub.h.
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "pty_tel_stub.h"

/* Project headers (available because the CMakeLists links tg-proto + tg-app) */
#include "crypto.h"
#include "mtproto_crypto.h"
#include "mtproto_session.h"
#include "tl_serial.h"
#include "app/session_store.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── TL / MTProto constants ─────────────────────────────────────────── */

#define CRC_invokeWithLayer       0xda9b0d0dU
#define CRC_initConnection        0xc1cd5ea9U
#define CRC_rpc_result            0xf35c6d01U
#define CRC_rpc_error             0x2144ca19U
#define CRC_messages_getDialogs   0xa0f4cb4fU
#define CRC_updates_getState      0xedd4882aU
#define CRC_updates_getDifference 0x19c2f763U
#define TL_messages_dialogs       0x15ba6c40U
#define TL_vector                 0x1cb5c415U

#define FRAME_MAX  (256 * 1024)
#define AUTH_KEY_SIZE PTY_STUB_AUTH_KEY_SIZE

/* ── Internal per-connection context ────────────────────────────────── */

typedef struct {
    PtyTelStub *stub;
    int         fd;
    uint64_t    next_msg_id;
    uint32_t    seq_no;
} ConnCtx;

/* ── Helpers ─────────────────────────────────────────────────────────── */

static uint64_t derive_auth_key_id(const uint8_t *key) {
    uint8_t hash[32];
    crypto_sha256(key, AUTH_KEY_SIZE, hash);
    uint64_t id = 0;
    for (int i = 0; i < 8; ++i) id |= ((uint64_t)hash[24 + i]) << (i * 8);
    return id;
}

static uint64_t make_server_msg_id(ConnCtx *c) {
    uint64_t now = (uint64_t)time(NULL) << 32;
    if (now <= c->next_msg_id) now = c->next_msg_id + 4;
    now &= ~((uint64_t)3);
    now |= 1;
    c->next_msg_id = now;
    return now;
}

/* Blocking full-read from fd */
static int read_all(int fd, uint8_t *buf, size_t len) {
    size_t done = 0;
    while (done < len) {
        ssize_t r = recv(fd, buf + done, len - done, 0);
        if (r <= 0) return -1;
        done += (size_t)r;
    }
    return 0;
}

/* Blocking full-write to fd */
static int write_all(int fd, const uint8_t *buf, size_t len) {
    size_t done = 0;
    while (done < len) {
        ssize_t r = send(fd, buf + done, len - done, 0);
        if (r <= 0) return -1;
        done += (size_t)r;
    }
    return 0;
}

/* ── Frame I/O ───────────────────────────────────────────────────────── */

/** Read one abridged-framed MTProto payload from fd into *buf_out (malloc'd). */
static int read_frame(int fd, uint8_t **buf_out, size_t *len_out) {
    uint8_t first;
    if (read_all(fd, &first, 1) != 0) return -1;

    size_t units;
    if (first < 0x7F) {
        units = first;
    } else {
        uint8_t extra[3];
        if (read_all(fd, extra, 3) != 0) return -1;
        units = (size_t)extra[0] | ((size_t)extra[1] << 8) | ((size_t)extra[2] << 16);
    }
    size_t len = units * 4;
    if (len == 0 || len > FRAME_MAX) return -1;

    uint8_t *buf = (uint8_t *)malloc(len);
    if (!buf) return -1;
    if (read_all(fd, buf, len) != 0) { free(buf); return -1; }

    *buf_out = buf;
    *len_out = len;
    return 0;
}

/** Write a plaintext body as an abridged-framed encrypted MTProto message. */
static int send_frame(ConnCtx *c, const uint8_t *body, size_t body_len) {
    /* Build plaintext wrapper */
    TlWriter plain;
    tl_writer_init(&plain);
    tl_write_uint64(&plain, c->stub->server_salt);
    tl_write_uint64(&plain, c->stub->session_id);
    tl_write_uint64(&plain, make_server_msg_id(c));
    c->seq_no += 2;
    tl_write_uint32(&plain, c->seq_no - 1);
    tl_write_uint32(&plain, (uint32_t)body_len);
    tl_write_raw(&plain, body, body_len);

    /* Encrypt */
    uint8_t msg_key[16];
    size_t enc_len = 0;
    uint8_t *enc = (uint8_t *)malloc(plain.len + 1024);
    if (!enc) { tl_writer_free(&plain); return -1; }
    mtproto_encrypt(plain.data, plain.len, c->stub->auth_key, 8,
                    enc, &enc_len, msg_key);
    tl_writer_free(&plain);

    /* Build wire frame: auth_key_id(8) + msg_key(16) + enc */
    size_t wire_len = 8 + 16 + enc_len;
    uint8_t *wire = (uint8_t *)malloc(wire_len);
    if (!wire) { free(enc); return -1; }
    for (int i = 0; i < 8; ++i)
        wire[i] = (uint8_t)((c->stub->auth_key_id >> (i * 8)) & 0xFF);
    memcpy(wire + 8, msg_key, 16);
    memcpy(wire + 24, enc, enc_len);
    free(enc);

    /* Abridged length prefix */
    size_t units = wire_len / 4;
    int rc;
    if (units < 0x7F) {
        uint8_t p = (uint8_t)units;
        rc = write_all(c->fd, &p, 1);
    } else {
        uint8_t p[4] = { 0x7F,
            (uint8_t)(units & 0xFF),
            (uint8_t)((units >> 8) & 0xFF),
            (uint8_t)((units >> 16) & 0xFF) };
        rc = write_all(c->fd, p, 4);
    }
    if (rc == 0) rc = write_all(c->fd, wire, wire_len);
    free(wire);
    return rc;
}

/* ── RPC helpers ─────────────────────────────────────────────────────── */

static void reply_rpc_result(ConnCtx *c, uint64_t req_msg_id,
                              const uint8_t *result, size_t result_len) {
    size_t total = 4 + 8 + result_len;
    uint8_t *wrapped = (uint8_t *)malloc(total);
    if (!wrapped) return;
    wrapped[0] = (uint8_t)(CRC_rpc_result);
    wrapped[1] = (uint8_t)(CRC_rpc_result >> 8);
    wrapped[2] = (uint8_t)(CRC_rpc_result >> 16);
    wrapped[3] = (uint8_t)(CRC_rpc_result >> 24);
    for (int i = 0; i < 8; ++i)
        wrapped[4 + i] = (uint8_t)((req_msg_id >> (i * 8)) & 0xFF);
    memcpy(wrapped + 12, result, result_len);
    send_frame(c, wrapped, total);
    free(wrapped);
}

static void reply_error(ConnCtx *c, uint64_t req_msg_id,
                         int32_t code, const char *msg) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_rpc_error);
    tl_write_int32(&w, code);
    tl_write_string(&w, msg ? msg : "");
    reply_rpc_result(c, req_msg_id, w.data, w.len);
    tl_writer_free(&w);
}

/** Respond with empty messages.dialogs. */
static void reply_empty_dialogs(ConnCtx *c, uint64_t req_msg_id) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_messages_dialogs);
    /* dialogs: Vector<Dialog> — empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);
    /* messages: Vector<Message> — empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);
    /* chats: Vector<Chat> — empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);
    /* users: Vector<User> — empty */
    tl_write_uint32(&w, TL_vector);
    tl_write_uint32(&w, 0);
    reply_rpc_result(c, req_msg_id, w.data, w.len);
    tl_writer_free(&w);
}

/* ── Frame dispatcher ────────────────────────────────────────────────── */

static void dispatch(ConnCtx *c, uint64_t req_msg_id,
                     const uint8_t *body, size_t body_len) {
    if (body_len < 4) return;

    /* Skip invokeWithLayer / initConnection wrappers (up to 3 levels deep) */
    const uint8_t *cur = body;
    size_t remaining = body_len;
    for (int depth = 0; depth < 3; ++depth) {
        if (remaining < 4) return;
        uint32_t crc = (uint32_t)cur[0] | ((uint32_t)cur[1] << 8)
                     | ((uint32_t)cur[2] << 16) | ((uint32_t)cur[3] << 24);
        if (crc == CRC_invokeWithLayer) {
            if (remaining < 8) return;
            cur += 8; remaining -= 8; /* CRC(4) + layer(4) */
            continue;
        }
        if (crc == CRC_initConnection) {
            /* Skip: CRC(4) flags(4) api_id(4) 6×string query */
            if (remaining < 16) return;
            size_t skip = 12; /* CRC + flags + api_id */
            /* Skip 6 TL strings (device_model, sys_version, app_version,
             * system_lang_code, lang_pack, lang_code) */
            for (int s = 0; s < 6 && skip < remaining; ++s) {
                uint8_t slen = cur[skip++];
                if (slen == 0xFE) {
                    if (skip + 3 > remaining) return;
                    size_t n3 = (size_t)cur[skip] | ((size_t)cur[skip+1] << 8)
                              | ((size_t)cur[skip+2] << 16);
                    skip += 3 + n3;
                    /* align to 4 bytes from start of string prefix */
                    size_t total_str = 4 + n3;
                    size_t pad = (4 - (total_str % 4)) % 4;
                    skip += pad;
                } else {
                    skip += (size_t)slen;
                    /* align: total = 1 + slen, pad to multiple of 4 */
                    size_t total_str = 1 + (size_t)slen;
                    size_t pad = (4 - (total_str % 4)) % 4;
                    skip += pad;
                }
            }
            if (skip > remaining) return;
            cur += skip; remaining -= skip;
            continue;
        }
        break;
    }

    if (remaining < 4) return;
    uint32_t inner_crc = (uint32_t)cur[0] | ((uint32_t)cur[1] << 8)
                       | ((uint32_t)cur[2] << 16) | ((uint32_t)cur[3] << 24);

    if (inner_crc == CRC_messages_getDialogs) {
        reply_empty_dialogs(c, req_msg_id);
    } else if (inner_crc == CRC_updates_getState) {
        reply_error(c, req_msg_id, 400, "STUB_NOT_SUPPORTED");
    } else if (inner_crc == CRC_updates_getDifference) {
        reply_error(c, req_msg_id, 400, "STUB_NOT_SUPPORTED");
    } else {
        /* Unknown RPC — return an error so the client doesn't hang on recv. */
        char msg[64];
        snprintf(msg, sizeof(msg), "STUB_UNKNOWN_%08x", inner_crc);
        reply_error(c, req_msg_id, 500, msg);
    }
}

/* ── Connection handler ──────────────────────────────────────────────── */

static void handle_connection(ConnCtx *c) {
    /* 1. Read the 0xEF abridged marker. */
    uint8_t marker;
    if (read_all(c->fd, &marker, 1) != 0 || marker != 0xEF) return;

    /* 2. Read encrypted frames until the connection closes or we decide to stop. */
    for (;;) {
        uint8_t *frame = NULL;
        size_t frame_len = 0;
        if (read_frame(c->fd, &frame, &frame_len) != 0) break;

        /* Validate auth_key_id */
        if (frame_len < 24) { free(frame); break; }
        uint64_t key_id = 0;
        for (int i = 0; i < 8; ++i) key_id |= ((uint64_t)frame[i]) << (i * 8);
        if (key_id != c->stub->auth_key_id) { free(frame); continue; }

        /* Decrypt */
        uint8_t msg_key[16];
        memcpy(msg_key, frame + 8, 16);
        const uint8_t *cipher = frame + 24;
        size_t cipher_len = frame_len - 24;
        uint8_t *plain = (uint8_t *)malloc(cipher_len);
        if (!plain) { free(frame); break; }
        size_t plain_len = 0;
        int rc = mtproto_decrypt(cipher, cipher_len,
                                  c->stub->auth_key, msg_key, 0,
                                  plain, &plain_len);
        free(frame);
        if (rc != 0) { free(plain); continue; }

        /* Extract msg_id and body from plaintext header:
         * salt(8) + session_id(8) + msg_id(8) + seq_no(4) + body_len(4) + body */
        if (plain_len < 32) { free(plain); continue; }
        uint64_t msg_id = 0;
        for (int i = 0; i < 8; ++i) msg_id |= ((uint64_t)plain[16 + i]) << (i * 8);
        uint32_t body_len = 0;
        for (int i = 0; i < 4; ++i) body_len |= ((uint32_t)plain[28 + i]) << (i * 8);
        if (32 + body_len > plain_len) { free(plain); continue; }

        dispatch(c, msg_id, plain + 32, body_len);
        free(plain);
    }
}

/* ── Server thread ───────────────────────────────────────────────────── */

static void *server_thread(void *arg) {
    PtyTelStub *s = (PtyTelStub *)arg;

    struct timeval tv = { .tv_sec = 6, .tv_usec = 0 };
    int client_fd = accept(s->listen_fd, NULL, NULL);
    if (client_fd < 0) return NULL;

    /* 6-second read timeout on the client socket so the thread exits cleanly
     * when the binary quits and stops sending. */
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ConnCtx ctx = {
        .stub        = s,
        .fd          = client_fd,
        .next_msg_id = 0,
        .seq_no      = 0,
    };
    handle_connection(&ctx);
    close(client_fd);
    return NULL;
}

/* ── Public API ──────────────────────────────────────────────────────── */

int pty_tel_stub_start(PtyTelStub *s) {
    if (!s) return -1;
    memset(s, 0, sizeof(*s));
    s->listen_fd = -1;

    /* Deterministic auth_key — identical to mt_server_seed_session(). */
    for (int i = 0; i < AUTH_KEY_SIZE; ++i)
        s->auth_key[i] = (uint8_t)((i * 31 + 7) & 0xFF);
    s->auth_key_id = derive_auth_key_id(s->auth_key);
    s->server_salt = 0xABCDEF0123456789ULL;
    s->session_id  = 0x1122334455667788ULL;

    /* Seed session.bin — caller must have set HOME to a tmp dir first. */
    MtProtoSession ms;
    mtproto_session_init(&ms);
    mtproto_session_set_auth_key(&ms, s->auth_key);
    mtproto_session_set_salt(&ms, s->server_salt);
    ms.session_id = s->session_id;
    if (session_store_save(&ms, 2) != 0) return -1;

    /* Bind to an OS-assigned port on localhost. */
    s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s->listen_fd < 0) return -1;

    int opt = 1;
    setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0; /* OS assigns */

    if (bind(s->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s->listen_fd); s->listen_fd = -1; return -1;
    }
    if (listen(s->listen_fd, 1) < 0) {
        close(s->listen_fd); s->listen_fd = -1; return -1;
    }

    /* Discover the assigned port. */
    socklen_t len = sizeof(addr);
    if (getsockname(s->listen_fd, (struct sockaddr *)&addr, &len) < 0) {
        close(s->listen_fd); s->listen_fd = -1; return -1;
    }
    s->port = ntohs(addr.sin_port);

    /* Set accept() timeout so the thread doesn't block forever if no client
     * ever connects. */
    struct timeval tv = { .tv_sec = 8, .tv_usec = 0 };
    setsockopt(s->listen_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    s->running = 1;
    if (pthread_create(&s->thread, NULL, server_thread, s) != 0) {
        close(s->listen_fd); s->listen_fd = -1; return -1;
    }
    return 0;
}

void pty_tel_stub_stop(PtyTelStub *s) {
    if (!s || !s->running) return;
    s->running = 0;
    if (s->listen_fd >= 0) {
        close(s->listen_fd);
        s->listen_fd = -1;
    }
    pthread_join(s->thread, NULL);
}
