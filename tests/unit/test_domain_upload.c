/**
 * @file test_domain_upload.c
 * @brief Unit tests for domain_send_file (US-P6-02).
 */

#include "test_helpers.h"
#include "domain/write/upload.h"
#include "tl_serial.h"
#include "tl_registry.h"
#include "mock_socket.h"
#include "mock_crypto.h"
#include "mtproto_session.h"
#include "transport.h"
#include "api_call.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void build_fake_encrypted_response(const uint8_t *payload, size_t plen,
                                          uint8_t *out, size_t *out_len) {
    TlWriter w; tl_writer_init(&w);
    uint8_t zeros24[24] = {0}; tl_write_raw(&w, zeros24, 24);
    uint8_t header[32] = {0};
    uint32_t plen32 = (uint32_t)plen;
    memcpy(header + 28, &plen32, 4);
    tl_write_raw(&w, header, 32);
    tl_write_raw(&w, payload, plen);
    size_t enc = w.len - 24;
    if (enc % 16 != 0) {
        uint8_t pad[16] = {0}; tl_write_raw(&w, pad, 16 - (enc % 16));
    }
    size_t dwords = w.len / 4;
    size_t off = 0;
    if (dwords < 0x7F) { out[0] = (uint8_t)dwords; off = 1; }
    else {
        out[0] = 0x7F;
        out[1] = (uint8_t)dwords;
        out[2] = (uint8_t)(dwords >> 8);
        out[3] = (uint8_t)(dwords >> 16);
        off = 4;
    }
    memcpy(out + off, w.data, w.len);
    *out_len = off + w.len;
    tl_writer_free(&w);
}

static void fix_session(MtProtoSession *s) {
    mtproto_session_init(s);
    uint8_t k[256] = {0}; mtproto_session_set_auth_key(s, k);
    mtproto_session_set_salt(s, 0xBADCAFEDEADBEEFULL);
}
static void fix_transport(Transport *t) {
    transport_init(t); t->fd = 42; t->connected = 1; t->dc_id = 1;
}
static void fix_cfg(ApiConfig *cfg) {
    api_config_init(cfg); cfg->api_id = 12345; cfg->api_hash = "deadbeef";
}

/* Write a small temp file, return its path (caller unlinks). */
static void write_temp(const char *path, const uint8_t *data, size_t n) {
    FILE *fp = fopen(path, "wb");
    if (fp) { fwrite(data, 1, n, fp); fclose(fp); }
}

/* Build a response buffer carrying two frames: boolTrue (for
 * saveFilePart) followed by updateShort (for sendMedia). */
static void seed_two_frame_response(void) {
    mock_socket_reset();

    /* frame 1: boolTrue */
    TlWriter w1; tl_writer_init(&w1);
    tl_write_uint32(&w1, TL_boolTrue);
    uint8_t resp1[128]; size_t r1 = 0;
    build_fake_encrypted_response(w1.data, w1.len, resp1, &r1);
    tl_writer_free(&w1);

    /* frame 2: updateShort (crc + 4 bytes update + 4 bytes date) */
    TlWriter w2; tl_writer_init(&w2);
    tl_write_uint32(&w2, TL_updateShort);
    tl_write_uint32(&w2, 0);
    tl_write_int32 (&w2, 1700000000);
    uint8_t resp2[128]; size_t r2 = 0;
    build_fake_encrypted_response(w2.data, w2.len, resp2, &r2);
    tl_writer_free(&w2);

    mock_socket_set_response(resp1, r1);
    mock_socket_append_response(resp2, r2);
}

static void test_upload_small_file_success(void) {
    mock_crypto_reset();
    const char *path = "/tmp/tg-cli-upload-test.bin";
    unlink(path);
    uint8_t body[200];
    for (size_t i = 0; i < sizeof(body); i++) body[i] = (uint8_t)i;
    write_temp(path, body, sizeof(body));

    seed_two_frame_response();

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };
    RpcError err = {0};
    int rc = domain_send_file(&cfg, &s, &t, &peer, path, "hello", NULL, &err);
    ASSERT(rc == 0, "upload ok for a small file");
    unlink(path);
}

static void test_upload_rejects_missing_file(void) {
    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };
    int rc = domain_send_file(&cfg, &s, &t, &peer,
                                "/tmp/tg-cli-no-such-file", NULL, NULL, NULL);
    ASSERT(rc == -1, "missing file rejected");
}

static void test_upload_rejects_empty_file(void) {
    const char *path = "/tmp/tg-cli-upload-empty.bin";
    unlink(path);
    FILE *fp = fopen(path, "wb"); if (fp) fclose(fp);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };
    int rc = domain_send_file(&cfg, &s, &t, &peer, path, NULL, NULL, NULL);
    ASSERT(rc == -1, "empty file rejected");
    unlink(path);
}

static void test_upload_null_args(void) {
    ASSERT(domain_send_file(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == -1,
           "null args rejected");
}

/* Wire-inspection: the outbound buffer must contain the
 * upload.saveFilePart CRC and the filename + mime type. */
static void test_upload_writes_expected_bytes(void) {
    mock_crypto_reset();
    const char *path = "/tmp/tg-cli-upload-inspect.bin";
    unlink(path);
    uint8_t body[32];
    for (size_t i = 0; i < sizeof(body); i++) body[i] = (uint8_t)(i + 1);
    write_temp(path, body, sizeof(body));

    seed_two_frame_response();

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };
    int rc = domain_send_file(&cfg, &s, &t, &peer, path,
                                "hi", "text/plain", NULL);
    ASSERT(rc == 0, "upload ok");

    size_t sent_len = 0;
    const uint8_t *sent = mock_socket_get_sent(&sent_len);

    uint32_t crc_part = 0xb304a621u;
    int found_part = 0;
    for (size_t i = 0; i + 4 <= sent_len; i++)
        if (memcmp(sent + i, &crc_part, 4) == 0) { found_part = 1; break; }
    ASSERT(found_part, "upload.saveFilePart CRC present on the wire");

    int found_name = 0;
    const char *basename = "tg-cli-upload-inspect.bin";
    for (size_t i = 0; i + strlen(basename) <= sent_len; i++)
        if (memcmp(sent + i, basename, strlen(basename)) == 0) {
            found_name = 1; break;
        }
    ASSERT(found_name, "filename appears in sendMedia payload");

    int found_mime = 0;
    const char *mime = "text/plain";
    for (size_t i = 0; i + strlen(mime) <= sent_len; i++)
        if (memcmp(sent + i, mime, strlen(mime)) == 0) {
            found_mime = 1; break;
        }
    ASSERT(found_mime, "mime_type appears in sendMedia payload");

    unlink(path);
}

void run_domain_upload_tests(void) {
    RUN_TEST(test_upload_small_file_success);
    RUN_TEST(test_upload_rejects_missing_file);
    RUN_TEST(test_upload_rejects_empty_file);
    RUN_TEST(test_upload_null_args);
    RUN_TEST(test_upload_writes_expected_bytes);
}
