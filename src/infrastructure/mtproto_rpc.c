/**
 * @file mtproto_rpc.c
 * @brief MTProto RPC framework — message framing, encryption, decryption.
 */

#include "mtproto_rpc.h"
#include "mtproto_crypto.h"
#include "tl_serial.h"
#include "crypto.h"
#include "tinf.h"

#include <stdlib.h>
#include <string.h>

#define CRC_gzip_packed    0x3072cfa1
#define CRC_msg_container  0x73f1f8dc
#define CRC_rpc_result     0xf35c6d01
#define CRC_rpc_error      0x2144ca19

/* ---- Unencrypted messages ---- */

int rpc_send_unencrypted(MtProtoSession *s, Transport *t,
                         const uint8_t *data, size_t len) {
    if (!s || !t || !data) return -1;

    uint64_t msg_id = mtproto_session_next_msg_id(s);

    /* Wire format: auth_key_id(8) + msg_id(8) + len(4) + data */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint64(&w, 0);            /* auth_key_id = 0 */
    tl_write_uint64(&w, msg_id);
    tl_write_uint32(&w, (uint32_t)len);
    tl_write_raw(&w, data, len);

    int rc = transport_send(t, w.data, w.len);
    tl_writer_free(&w);
    return rc;
}

int rpc_recv_unencrypted(MtProtoSession *s, Transport *t,
                         uint8_t *out, size_t max_len, size_t *out_len) {
    if (!s || !t || !out || !out_len) return -1;

    /* Read one transport packet */
    uint8_t buf[65536];
    size_t buf_len = 0;
    if (transport_recv(t, buf, sizeof(buf), &buf_len) != 0) return -1;

    /* Parse: auth_key_id(8) + msg_id(8) + len(4) + data */
    if (buf_len < 20) return -1;

    TlReader r = tl_reader_init(buf, buf_len);
    uint64_t auth_key_id = tl_read_uint64(&r);
    (void)auth_key_id; /* should be 0 */
    uint64_t msg_id = tl_read_uint64(&r);
    (void)msg_id;
    uint32_t data_len = tl_read_uint32(&r);

    if (data_len > max_len) return -1;
    if (data_len > 0) {
        tl_read_raw(&r, out, data_len);
    }
    *out_len = data_len;
    return 0;
}

/* ---- Encrypted messages ---- */

int rpc_send_encrypted(MtProtoSession *s, Transport *t,
                       const uint8_t *data, size_t len,
                       int content_related) {
    if (!s || !t || !data || !s->has_auth_key) return -1;

    uint64_t msg_id = mtproto_session_next_msg_id(s);
    uint32_t seq_no = mtproto_session_next_seq_no(s, content_related);

    /* Build plaintext payload: salt(8) + session_id(8) + msg_id(8) + seq_no(4) + len(4) + data */
    TlWriter plain;
    tl_writer_init(&plain);
    tl_write_uint64(&plain, s->server_salt);
    tl_write_uint64(&plain, s->session_id);
    tl_write_uint64(&plain, msg_id);
    tl_write_uint32(&plain, seq_no);
    tl_write_uint32(&plain, (uint32_t)len);
    tl_write_raw(&plain, data, len);

    /* Encrypt with MTProto crypto */
    uint8_t encrypted[65536];
    size_t enc_len = 0;
    mtproto_encrypt(plain.data, plain.len, s->auth_key, 0, encrypted, &enc_len);
    tl_writer_free(&plain);

    /* Compute msg_key from plaintext */
    TlWriter plain2;
    tl_writer_init(&plain2);
    tl_write_uint64(&plain2, s->server_salt);
    tl_write_uint64(&plain2, s->session_id);
    tl_write_uint64(&plain2, msg_id);
    tl_write_uint32(&plain2, seq_no);
    tl_write_uint32(&plain2, (uint32_t)len);
    tl_write_raw(&plain2, data, len);

    uint8_t msg_key[16];
    mtproto_compute_msg_key(s->auth_key, plain2.data, plain2.len, 0, msg_key);
    tl_writer_free(&plain2);

    /* Wire format: auth_key_id(8) + msg_key(16) + encrypted_data */
    TlWriter wire;
    tl_writer_init(&wire);

    /* auth_key_id = lower 8 bytes of SHA1(auth_key) */
    uint8_t key_hash[32];
    crypto_sha256(s->auth_key, 256, key_hash);
    tl_write_uint64(&wire, *(uint64_t *)(key_hash + 24)); /* last 8 bytes */

    tl_write_raw(&wire, msg_key, 16);
    tl_write_raw(&wire, encrypted, enc_len);

    int rc = transport_send(t, wire.data, wire.len);
    tl_writer_free(&wire);
    return rc;
}

int rpc_recv_encrypted(MtProtoSession *s, Transport *t,
                       uint8_t *out, size_t max_len, size_t *out_len) {
    if (!s || !t || !out || !out_len || !s->has_auth_key) return -1;

    /* Read one transport packet */
    uint8_t buf[65536];
    size_t buf_len = 0;
    if (transport_recv(t, buf, sizeof(buf), &buf_len) != 0) return -1;

    /* Parse: auth_key_id(8) + msg_key(16) + encrypted_data */
    if (buf_len < 24) return -1;

    TlReader r = tl_reader_init(buf, buf_len);
    tl_read_uint64(&r); /* auth_key_id — skip */

    uint8_t msg_key[16];
    tl_read_raw(&r, msg_key, 16);

    size_t cipher_len = buf_len - 24;
    const uint8_t *cipher = buf + 24;

    /* Decrypt */
    uint8_t decrypted[65536];
    size_t dec_len = 0;
    int rc = mtproto_decrypt(cipher, cipher_len, s->auth_key, msg_key, 8,
                             decrypted, &dec_len);
    if (rc != 0) return -1;

    /* Parse plaintext: salt(8) + session_id(8) + msg_id(8) + seq_no(4) + len(4) + data */
    if (dec_len < 32) return -1;

    TlReader pr = tl_reader_init(decrypted, dec_len);
    tl_read_uint64(&pr); /* salt */
    tl_read_uint64(&pr); /* session_id */
    tl_read_uint64(&pr); /* msg_id */
    tl_read_uint32(&pr); /* seq_no */
    uint32_t data_len = tl_read_uint32(&pr);

    if (data_len > max_len) return -1;
    if (data_len > 0) {
        tl_read_raw(&pr, out, data_len);
    }
    *out_len = data_len;
    return 0;
}

/* ---- gzip_packed unwrap ---- */

int rpc_unwrap_gzip(const uint8_t *data, size_t len,
                    uint8_t *out, size_t max_len, size_t *out_len) {
    if (!data || !out || !out_len) return -1;
    if (len < 4) {
        /* Too short to contain a constructor — copy as-is */
        if (len > max_len) return -1;
        memcpy(out, data, len);
        *out_len = len;
        return 0;
    }

    /* Check for gzip_packed constructor */
    uint32_t constructor;
    memcpy(&constructor, data, 4);

    if (constructor != CRC_gzip_packed) {
        /* Not gzip_packed — copy unchanged */
        if (len > max_len) return -1;
        memcpy(out, data, len);
        *out_len = len;
        return 0;
    }

    /* Parse: constructor(4) + bytes(TL-encoded compressed data) */
    TlReader r = tl_reader_init(data, len);
    tl_read_uint32(&r); /* skip constructor */

    size_t gz_len = 0;
    uint8_t *gz_data = tl_read_bytes(&r, &gz_len);
    if (!gz_data || gz_len == 0) {
        free(gz_data);
        return -1;
    }

    /* Decompress with tinf */
    unsigned int dest_len = (unsigned int)max_len;
    int rc = tinf_gzip_uncompress(out, &dest_len,
                                   gz_data, (unsigned int)gz_len);
    free(gz_data);

    if (rc != TINF_OK) return -1;

    *out_len = dest_len;
    return 0;
}

/* ---- msg_container parse ---- */

int rpc_parse_container(const uint8_t *data, size_t len,
                        RpcContainerMsg *msgs, size_t max_msgs,
                        size_t *count) {
    if (!data || !msgs || !count) return -1;
    if (len < 4) return -1;

    uint32_t constructor;
    memcpy(&constructor, data, 4);

    if (constructor != CRC_msg_container) {
        /* Not a container — return as single message */
        if (max_msgs < 1) return -1;
        msgs[0].msg_id = 0;
        msgs[0].seqno = 0;
        msgs[0].body_len = (uint32_t)len;
        msgs[0].body = data;
        *count = 1;
        return 0;
    }

    /* Parse: constructor(4) + count(4) + messages[] */
    TlReader r = tl_reader_init(data, len);
    tl_read_uint32(&r); /* skip constructor */
    uint32_t msg_count = tl_read_uint32(&r);

    if (msg_count > max_msgs) return -1;

    for (uint32_t i = 0; i < msg_count; i++) {
        if (!tl_reader_ok(&r)) return -1;

        msgs[i].msg_id = tl_read_uint64(&r);
        msgs[i].seqno = tl_read_uint32(&r);
        msgs[i].body_len = tl_read_uint32(&r);

        if (msgs[i].body_len > len - r.pos) return -1;

        msgs[i].body = data + r.pos;
        tl_read_skip(&r, msgs[i].body_len);
    }

    *count = msg_count;
    return 0;
}

/* ---- rpc_result unwrap ---- */

int rpc_unwrap_result(const uint8_t *data, size_t len,
                      uint64_t *req_msg_id,
                      const uint8_t **inner, size_t *inner_len) {
    if (!data || !req_msg_id || !inner || !inner_len) return -1;
    if (len < 12) return -1; /* constructor(4) + msg_id(8) minimum */

    uint32_t constructor;
    memcpy(&constructor, data, 4);
    if (constructor != CRC_rpc_result) return -1;

    memcpy(req_msg_id, data + 4, 8);
    *inner = data + 12;
    *inner_len = len - 12;
    return 0;
}

/* ---- rpc_error parse ---- */

/** Extract trailing integer from error message (e.g. "FLOOD_WAIT_30" → 30). */
static int extract_trailing_int(const char *msg) {
    const char *p = msg + strlen(msg);
    while (p > msg && p[-1] >= '0' && p[-1] <= '9') p--;
    if (*p == '\0') return 0;
    return atoi(p);
}

int rpc_parse_error(const uint8_t *data, size_t len, RpcError *err) {
    if (!data || !err) return -1;
    if (len < 4) return -1;

    uint32_t constructor;
    memcpy(&constructor, data, 4);
    if (constructor != CRC_rpc_error) return -1;

    TlReader r = tl_reader_init(data, len);
    tl_read_uint32(&r); /* skip constructor */

    err->error_code = tl_read_int32(&r);

    char *msg = tl_read_string(&r);
    if (!msg) {
        memset(err->error_msg, 0, sizeof(err->error_msg));
        err->migrate_dc = -1;
        err->flood_wait_secs = 0;
        return 0;
    }

    /* Copy message (truncate if needed) */
    size_t msg_len = strlen(msg);
    if (msg_len >= sizeof(err->error_msg))
        msg_len = sizeof(err->error_msg) - 1;
    memcpy(err->error_msg, msg, msg_len);
    err->error_msg[msg_len] = '\0';

    /* Parse derived fields */
    err->migrate_dc = -1;
    err->flood_wait_secs = 0;

    if (strncmp(msg, "PHONE_MIGRATE_", 14) == 0 ||
        strncmp(msg, "FILE_MIGRATE_", 13) == 0 ||
        strncmp(msg, "NETWORK_MIGRATE_", 16) == 0 ||
        strncmp(msg, "USER_MIGRATE_", 13) == 0) {
        err->migrate_dc = extract_trailing_int(msg);
    } else if (strncmp(msg, "FLOOD_WAIT_", 11) == 0) {
        err->flood_wait_secs = extract_trailing_int(msg);
    }

    free(msg);
    return 0;
}
