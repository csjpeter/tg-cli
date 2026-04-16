/**
 * @file test_domain_media.c
 * @brief Unit tests for domain_download_photo (P6-01).
 */

#include "test_helpers.h"
#include "domain/read/media.h"
#include "tl_serial.h"
#include "tl_registry.h"
#include "tl_skip.h"
#include "mock_socket.h"
#include "mock_crypto.h"
#include "mtproto_session.h"
#include "transport.h"
#include "api_call.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Build an encrypted frame compatible with mtproto_rpc.c's decrypt path
 * (same recipe used by test_domain_history.c). */
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
    out[0] = (uint8_t)(w.len / 4);
    memcpy(out + 1, w.data, w.len);
    *out_len = 1 + w.len;
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

#define CRC_upload_file 0x096a18d5u
#define CRC_storage_fileJpeg 0x007efe0eu

/* Build one upload.file response carrying @p body_len bytes. */
static size_t make_upload_file(uint8_t *buf, size_t max,
                               const uint8_t *body, size_t body_len) {
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, CRC_upload_file);
    tl_write_uint32(&w, CRC_storage_fileJpeg);
    tl_write_int32 (&w, 1700000000);           /* mtime */
    tl_write_bytes (&w, body, body_len);
    size_t n = w.len < max ? w.len : max;
    memcpy(buf, w.data, n);
    tl_writer_free(&w);
    return n;
}

static MediaInfo fake_photo_info(void) {
    MediaInfo m = {0};
    m.kind = MEDIA_PHOTO;
    m.photo_id = 123456789LL;
    m.access_hash = 0xDEADBEEFCAFEBABEULL;
    m.dc_id = 1;
    m.file_reference_len = 4;
    m.file_reference[0] = 0xAA;
    m.file_reference[1] = 0xBB;
    m.file_reference[2] = 0xCC;
    m.file_reference[3] = 0xDD;
    strncpy(m.thumb_type, "y", sizeof(m.thumb_type) - 1);
    return m;
}

/* Single-chunk download: body smaller than CHUNK_SIZE, loop exits after
 * one iteration. */
static void test_download_photo_single_chunk(void) {
    mock_socket_reset(); mock_crypto_reset();

    const char *path = "/tmp/tg-cli-media-test.jpg";
    unlink(path);

    uint8_t body[128];
    for (size_t i = 0; i < sizeof(body); i++) body[i] = (uint8_t)(i ^ 0x5A);

    uint8_t payload[512];
    size_t plen = make_upload_file(payload, sizeof(payload),
                                    body, sizeof(body));
    uint8_t resp[1024]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    MediaInfo info = fake_photo_info();
    int wrong_dc = -1;
    int rc = domain_download_photo(&cfg, &s, &t, &info, path, &wrong_dc);
    ASSERT(rc == 0, "single chunk download succeeds");
    ASSERT(wrong_dc == 0, "wrong_dc stays 0 on success");

    FILE *fp = fopen(path, "rb");
    ASSERT(fp != NULL, "file created on disk");
    if (fp) {
        uint8_t got[256] = {0};
        size_t n = fread(got, 1, sizeof(got), fp);
        fclose(fp);
        ASSERT(n == sizeof(body), "file size matches body");
        ASSERT(memcmp(got, body, sizeof(body)) == 0, "body round-trips");
    }
    unlink(path);
}

static void test_download_photo_rpc_error_migrate(void) {
    mock_socket_reset(); mock_crypto_reset();

    const char *path = "/tmp/tg-cli-media-migrate.jpg";
    unlink(path);

    /* Craft an RPC error with FILE_MIGRATE_2. */
    uint8_t payload[128];
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_rpc_error);
    tl_write_int32 (&w, 303);
    tl_write_string(&w, "FILE_MIGRATE_2");
    memcpy(payload, w.data, w.len);
    size_t plen = w.len;
    tl_writer_free(&w);

    uint8_t resp[512]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    MediaInfo info = fake_photo_info();
    int wrong_dc = 0;
    int rc = domain_download_photo(&cfg, &s, &t, &info, path, &wrong_dc);
    ASSERT(rc != 0, "RPC error propagates");
    ASSERT(wrong_dc == 2, "migrate_dc extracted from FILE_MIGRATE_2");
    unlink(path);
}

static void test_download_photo_rejects_non_photo(void) {
    ApiConfig cfg; MtProtoSession s; Transport t;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    MediaInfo info = fake_photo_info();
    info.kind = MEDIA_DOCUMENT;
    int wrong_dc = 0;
    int rc = domain_download_photo(&cfg, &s, &t, &info,
                                    "/tmp/tg-cli-media-x.jpg", &wrong_dc);
    ASSERT(rc != 0, "non-photo kind rejected");
}

static void test_download_photo_null_args(void) {
    int wrong_dc = 0;
    ASSERT(domain_download_photo(NULL, NULL, NULL, NULL, NULL, &wrong_dc) == -1,
           "null args rejected");
}

static void test_download_photo_requires_credentials(void) {
    ApiConfig cfg; MtProtoSession s; Transport t;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);

    MediaInfo info = {0};
    info.kind = MEDIA_PHOTO;
    /* photo_id=0 means info is incomplete — must fail before any RPC. */
    int wrong_dc = 0;
    int rc = domain_download_photo(&cfg, &s, &t, &info,
                                    "/tmp/tg-cli-media-x.jpg", &wrong_dc);
    ASSERT(rc != 0, "empty MediaInfo rejected");
}

void run_domain_media_tests(void) {
    RUN_TEST(test_download_photo_single_chunk);
    RUN_TEST(test_download_photo_rpc_error_migrate);
    RUN_TEST(test_download_photo_rejects_non_photo);
    RUN_TEST(test_download_photo_null_args);
    RUN_TEST(test_download_photo_requires_credentials);
}
