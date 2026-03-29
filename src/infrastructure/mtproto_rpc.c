/**
 * @file mtproto_rpc.c
 * @brief MTProto RPC framework — message framing, encryption, decryption.
 */

#include "mtproto_rpc.h"
#include "mtproto_crypto.h"
#include "tl_serial.h"
#include "crypto.h"

#include <stdlib.h>
#include <string.h>

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
