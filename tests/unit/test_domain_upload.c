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
    s->session_id = 0; /* match the zero session_id in fake encrypted frames */
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

/* Seed the mock socket with @p n boolTrue frames (for N saveBigFilePart
 * acks) followed by an updateShort frame (for the final sendMedia). */
static void seed_big_response(int n_parts) {
    mock_socket_reset();
    for (int i = 0; i < n_parts; i++) {
        TlWriter w; tl_writer_init(&w);
        tl_write_uint32(&w, TL_boolTrue);
        uint8_t resp[128]; size_t rlen = 0;
        build_fake_encrypted_response(w.data, w.len, resp, &rlen);
        tl_writer_free(&w);
        if (i == 0) mock_socket_set_response(resp, rlen);
        else        mock_socket_append_response(resp, rlen);
    }
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_updateShort);
    tl_write_uint32(&w, 0);
    tl_write_int32 (&w, 1700000000);
    uint8_t resp[128]; size_t rlen = 0;
    build_fake_encrypted_response(w.data, w.len, resp, &rlen);
    tl_writer_free(&w);
    mock_socket_append_response(resp, rlen);
}

static void test_upload_big_file_uses_saveBigFilePart(void) {
    mock_crypto_reset();
    const char *path = "/tmp/tg-cli-upload-big.bin";
    unlink(path);

    /* 12 MiB, just over the 10 MiB big threshold. */
    const size_t big_size = 12 * 1024 * 1024;
    uint8_t *body = (uint8_t *)malloc(big_size);
    ASSERT(body != NULL, "alloc 12 MiB body");
    for (size_t i = 0; i < big_size; i++) body[i] = (uint8_t)(i & 0xff);
    write_temp(path, body, big_size);
    free(body);

    int expected_parts = (int)((big_size + UPLOAD_CHUNK_SIZE - 1)
                                / UPLOAD_CHUNK_SIZE);
    seed_big_response(expected_parts);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };
    int rc = domain_send_file(&cfg, &s, &t, &peer, path, NULL, NULL, NULL);
    ASSERT(rc == 0, "big-file upload ok");

    size_t sent_len = 0;
    const uint8_t *sent = mock_socket_get_sent(&sent_len);

    uint32_t big_part = 0xde7b673du;
    int found_big = 0;
    for (size_t i = 0; i + 4 <= sent_len; i++)
        if (memcmp(sent + i, &big_part, 4) == 0) { found_big = 1; break; }
    ASSERT(found_big, "upload.saveBigFilePart CRC transmitted");

    uint32_t input_file_big = 0xfa4f0bb5u;
    int found_ifb = 0;
    for (size_t i = 0; i + 4 <= sent_len; i++)
        if (memcmp(sent + i, &input_file_big, 4) == 0) { found_ifb = 1; break; }
    ASSERT(found_ifb, "inputFileBig CRC on sendMedia");

    /* Plain saveFilePart CRC must NOT appear when is_big. */
    uint32_t small_part = 0xb304a621u;
    int found_small = 0;
    for (size_t i = 0; i + 4 <= sent_len; i++)
        if (memcmp(sent + i, &small_part, 4) == 0) {
            found_small = 1; break;
        }
    ASSERT(!found_small, "saveFilePart NOT used for big file");

    unlink(path);
}

static void test_upload_rejects_oversized_file(void) {
    /* Fabricate a file just over UPLOAD_MAX_SIZE by seeking — avoids
     * actually writing 1.5 GiB. */
    const char *path = "/tmp/tg-cli-upload-oversize.bin";
    unlink(path);
    FILE *fp = fopen(path, "wb");
    ASSERT(fp != NULL, "open oversize file");
    if (fp) {
        off_t too_big = (off_t)UPLOAD_MAX_SIZE + 1024;
        /* Seek then write one byte to create a sparse file. */
        if (fseeko(fp, too_big - 1, SEEK_SET) == 0) {
            fputc(0, fp);
        }
        fclose(fp);
    }

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };
    int rc = domain_send_file(&cfg, &s, &t, &peer, path, NULL, NULL, NULL);
    ASSERT(rc == -1, "over-cap file rejected");
    unlink(path);
}

/* saveFilePart returns a non-migrate RPC error — domain_send_file must
 * bail out rather than entering the cross-DC retry path. */
static void test_upload_non_migrate_error_bails(void) {
    mock_crypto_reset();

    const char *path = "/tmp/tg-cli-upload-err.bin";
    unlink(path);
    uint8_t body[64];
    for (size_t i = 0; i < sizeof(body); i++) body[i] = (uint8_t)i;
    write_temp(path, body, sizeof(body));

    /* Prime one frame with an FLOOD_WAIT_10 RPC error (no migrate_dc). */
    uint8_t payload[128];
    TlWriter w; tl_writer_init(&w);
    tl_write_uint32(&w, TL_rpc_error);
    tl_write_int32 (&w, 420);
    tl_write_string(&w, "FLOOD_WAIT_10");
    memcpy(payload, w.data, w.len);
    size_t plen = w.len;
    tl_writer_free(&w);

    uint8_t resp[512]; size_t rlen = 0;
    build_fake_encrypted_response(payload, plen, resp, &rlen);
    mock_socket_reset();
    mock_socket_set_response(resp, rlen);

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    int creates_before = mock_socket_was_created();

    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };
    int rc = domain_send_file(&cfg, &s, &t, &peer, path, NULL, NULL, NULL);
    ASSERT(rc == -1, "non-migrate error propagates");
    ASSERT(mock_socket_was_created() == creates_before,
           "no cross-DC socket opened for non-migrate error");
    unlink(path);
}

/* ---- LIM-01: photo upload ---- */

static void test_path_is_image_detects_common_extensions(void) {
    ASSERT(domain_path_is_image("foo.jpg") == 1, "jpg is image");
    ASSERT(domain_path_is_image("/a/b.JPEG") == 1, "JPEG case-insensitive");
    ASSERT(domain_path_is_image("/abc/x.png") == 1, "png is image");
    ASSERT(domain_path_is_image("/abc/x.Webp") == 1, "Webp case");
    ASSERT(domain_path_is_image("a.gif") == 1, "gif is image");
    ASSERT(domain_path_is_image("a.pdf") == 0, "pdf not image");
    ASSERT(domain_path_is_image("a.txt") == 0, "txt not image");
    ASSERT(domain_path_is_image("noext")  == 0, "no extension not image");
    ASSERT(domain_path_is_image("")       == 0, "empty not image");
    ASSERT(domain_path_is_image(NULL)     == 0, "NULL safe");
}

static void test_send_photo_small_success(void) {
    mock_crypto_reset();
    const char *path = "/tmp/tg-cli-photo.jpg";
    unlink(path);
    uint8_t body[256];
    for (size_t i = 0; i < sizeof(body); i++) body[i] = (uint8_t)(i * 3);
    write_temp(path, body, sizeof(body));

    seed_two_frame_response();

    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };
    RpcError err = {0};
    int rc = domain_send_photo(&cfg, &s, &t, &peer, path, "cheese", &err);
    ASSERT(rc == 0, "photo upload ok");

    size_t sent_len = 0;
    const uint8_t *sent = mock_socket_get_sent(&sent_len);

    /* inputMediaUploadedPhoto CRC must appear — not the document one. */
    uint32_t photo_crc = 0x1e287d04u;
    uint32_t doc_crc   = 0x5b38c6c1u;
    int found_photo = 0, found_doc = 0;
    for (size_t i = 0; i + 4 <= sent_len; i++) {
        if (memcmp(sent + i, &photo_crc, 4) == 0) found_photo = 1;
        if (memcmp(sent + i, &doc_crc,   4) == 0) found_doc = 1;
    }
    ASSERT(found_photo, "inputMediaUploadedPhoto on the wire");
    ASSERT(!found_doc, "inputMediaUploadedDocument NOT on the wire");

    unlink(path);
}

static void test_send_photo_null_args(void) {
    ASSERT(domain_send_photo(NULL, NULL, NULL, NULL, NULL, NULL, NULL) == -1,
           "null args rejected");
}

static void test_send_photo_rejects_missing_file(void) {
    MtProtoSession s; Transport t; ApiConfig cfg;
    fix_session(&s); fix_transport(&t); fix_cfg(&cfg);
    HistoryPeer peer = { .kind = HISTORY_PEER_SELF };
    int rc = domain_send_photo(&cfg, &s, &t, &peer,
                                "/tmp/tg-cli-no-such-photo", NULL, NULL);
    ASSERT(rc == -1, "missing photo rejected");
}

/* TEST-37: Empty TL string encoding of md5_checksum in inputFile.
 *
 * An empty TL string is encoded as: 0x00 (1-byte length prefix = 0) followed
 * by 3 zero padding bytes → 4 bytes total: { 0x00, 0x00, 0x00, 0x00 }.
 *
 * Build what write_input_file() would produce for a small (non-big) file and
 * verify the md5_checksum field appears as exactly these 4 bytes after the
 * filename TL string.  For inputFileBig, confirm those 4 bytes are absent
 * from the serialized body (the spec omits md5_checksum entirely).
 */
static void test_inputfile_md5_empty_tl_encoding(void) {
    /* Replicate write_input_file logic for a small file. */
    TlWriter w; tl_writer_init(&w);
    uint32_t crc_inputFile = 0xf52ff27fu;
    tl_write_uint32(&w, crc_inputFile);
    tl_write_int64 (&w, (int64_t)0x1122334455667788LL);  /* file_id */
    tl_write_int32 (&w, 1);                               /* part_count */
    tl_write_string(&w, "test.bin");                      /* file_name */
    tl_write_string(&w, "");                              /* md5_checksum */

    /* The last 4 bytes of the serialized buffer must be the empty TL string:
     * 0x00 (len=0) + 3 zero pad bytes. */
    ASSERT(w.len >= 4, "inputFile serialization has at least 4 bytes");
    const uint8_t *tail = w.data + w.len - 4;
    uint8_t expected[4] = {0x00, 0x00, 0x00, 0x00};
    ASSERT(memcmp(tail, expected, 4) == 0,
           "inputFile md5_checksum is 4 zero bytes (empty TL string)");

    /* Verify the CRC is at the start. */
    uint32_t got_crc = 0;
    memcpy(&got_crc, w.data, 4);
    ASSERT(got_crc == crc_inputFile, "inputFile CRC at offset 0");

    tl_writer_free(&w);
}

static void test_inputfilebig_has_no_md5_field(void) {
    /* Replicate write_input_file logic for a big file (no md5_checksum). */
    TlWriter w; tl_writer_init(&w);
    uint32_t crc_inputFileBig = 0xfa4f0bb5u;
    tl_write_uint32(&w, crc_inputFileBig);
    tl_write_int64 (&w, (int64_t)0xAABBCCDDEEFF0011LL); /* file_id */
    tl_write_int32 (&w, 3);                              /* part_count */
    tl_write_string(&w, "big.bin");                      /* file_name */
    /* No md5_checksum field for inputFileBig. */

    /* The last 4 bytes are the tail of the filename TL string, NOT an empty
     * md5 field.  Scan the entire buffer: an empty TL string (0 0 0 0) might
     * coincidentally appear in the padding of the filename, but it must NOT
     * be appended as an extra field.  The total length must match exactly the
     * layout without a trailing empty string. */

    /* Expected layout:
     *   4  CRC
     *   8  file_id
     *   4  part_count
     *   ?  tl_string("big.bin") = 1 + 7 + 0 pad = 8 bytes
     * Total = 24 bytes.  With md5 it would be 28. */
    ASSERT(w.len == 24, "inputFileBig serialized length is 24 (no md5 field)");

    uint32_t got_crc = 0;
    memcpy(&got_crc, w.data, 4);
    ASSERT(got_crc == crc_inputFileBig, "inputFileBig CRC at offset 0");

    tl_writer_free(&w);
}

void run_domain_upload_tests(void) {
    RUN_TEST(test_upload_small_file_success);
    RUN_TEST(test_upload_rejects_missing_file);
    RUN_TEST(test_upload_rejects_empty_file);
    RUN_TEST(test_upload_null_args);
    RUN_TEST(test_upload_writes_expected_bytes);
    RUN_TEST(test_upload_big_file_uses_saveBigFilePart);
    RUN_TEST(test_upload_rejects_oversized_file);
    RUN_TEST(test_upload_non_migrate_error_bails);
    RUN_TEST(test_path_is_image_detects_common_extensions);
    RUN_TEST(test_send_photo_small_success);
    RUN_TEST(test_send_photo_null_args);
    RUN_TEST(test_send_photo_rejects_missing_file);
    RUN_TEST(test_inputfile_md5_empty_tl_encoding);
    RUN_TEST(test_inputfilebig_has_no_md5_field);
}
