/**
 * @file test_upload_download.c
 * @brief FT-06 — upload / download functional tests.
 *
 * Drives the full end-to-end flow of:
 *   - upload.saveFilePart (small), upload.saveBigFilePart (>= 10 MiB),
 *     followed by messages.sendMedia (InputMediaUploadedDocument /
 *     InputMediaUploadedPhoto).
 *   - upload.getFile chunked download with EOF detection on short chunk.
 *   - FILE_MIGRATE_X surface via the `wrong_dc` out-parameter.
 *
 * Real AES-IGE + SHA-256 on both sides. The files live in /tmp and are
 * recreated per-test so concurrent runs don't collide.
 */

#include "test_helpers.h"

#include "mock_socket.h"
#include "mock_tel_server.h"

#include "api_call.h"
#include "mtproto_session.h"
#include "transport.h"
#include "app/session_store.h"
#include "tl_registry.h"
#include "tl_serial.h"

#include "domain/write/upload.h"
#include "domain/read/media.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CRC_upload_saveFilePart        0xb304a621U
#define CRC_upload_saveBigFilePart     0xde7b673dU
#define CRC_messages_sendMedia         0x7547c966U
#define CRC_upload_getFile             0xbe5335beU
#define CRC_upload_file                0x096a18d5U
#define CRC_storage_filePartial        0x40bc6f52U   /* storage.filePartial */
#define CRC_storage_fileJpeg           0x7efe0e   /* storage.fileJpeg */

static void with_tmp_home(const char *tag) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/tg-cli-ft-media-%s", tag);
    char bin[512];
    snprintf(bin, sizeof(bin), "%s/.config/tg-cli/session.bin", tmp);
    (void)unlink(bin);
    setenv("HOME", tmp, 1);
}

static void connect_mock(Transport *t) {
    transport_init(t);
    ASSERT(transport_connect(t, "127.0.0.1", 443) == 0, "connect");
}

static void init_cfg(ApiConfig *cfg) {
    api_config_init(cfg);
    cfg->api_id = 12345;
    cfg->api_hash = "deadbeefcafebabef00dbaadfeedc0de";
}

static void load_session(MtProtoSession *s) {
    ASSERT(mt_server_seed_session(2, NULL, NULL, NULL) == 0, "seed");
    mtproto_session_init(s);
    int dc = 0;
    ASSERT(session_store_load(s, &dc) == 0, "load");
}

/* Write a tempfile of @p size bytes filled with a deterministic byte
 * pattern and return its path (owned by a static buffer, caller must
 * unlink when done). */
static const char *make_tempfile(const char *name, size_t size) {
    static char path[256];
    snprintf(path, sizeof(path), "/tmp/tg-cli-fixture-%s.bin", name);
    FILE *fp = fopen(path, "wb");
    if (!fp) return NULL;
    uint8_t *buf = (uint8_t *)malloc(size);
    if (!buf) { fclose(fp); return NULL; }
    for (size_t i = 0; i < size; ++i) buf[i] = (uint8_t)(i & 0xFFu);
    size_t written = fwrite(buf, 1, size, fp);
    free(buf);
    fclose(fp);
    return (written == size) ? path : NULL;
}

/* ================================================================ */
/* Counters & state used by responders                              */
/* ================================================================ */

static int g_save_file_part_calls = 0;
static int g_save_big_file_part_calls = 0;
static int g_get_file_calls = 0;

static void reset_counters(void) {
    g_save_file_part_calls = 0;
    g_save_big_file_part_calls = 0;
    g_get_file_calls = 0;
}

/* ================================================================ */
/* Responders                                                       */
/* ================================================================ */

static void reply_bool_true(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_boolTrue);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

static void on_save_file_part(MtRpcContext *ctx) {
    g_save_file_part_calls++;
    reply_bool_true(ctx);
}

static void on_save_big_file_part(MtRpcContext *ctx) {
    g_save_big_file_part_calls++;
    reply_bool_true(ctx);
}

/* messages.sendMedia → minimal updates envelope. */
static void on_send_media(MtRpcContext *ctx) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, TL_updates);
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* updates */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* users */
    tl_write_uint32(&w, TL_vector); tl_write_uint32(&w, 0); /* chats */
    tl_write_int32 (&w, 0); tl_write_int32 (&w, 0);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* upload.file with a caller-supplied payload length. */
static void reply_upload_file(MtRpcContext *ctx,
                              const uint8_t *bytes, size_t len) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, CRC_upload_file);
    /* storage.FileType — pick storage.filePartial so no extension lookup */
    tl_write_uint32(&w, CRC_storage_filePartial);
    tl_write_int32 (&w, 0);                       /* mtime */
    tl_write_bytes (&w, bytes, len);
    mt_server_reply_result(ctx, w.data, w.len);
    tl_writer_free(&w);
}

/* Single-shot download: always returns a short chunk (EOF immediately). */
static void on_get_file_short(MtRpcContext *ctx) {
    g_get_file_calls++;
    uint8_t payload[128];
    for (size_t i = 0; i < sizeof(payload); ++i)
        payload[i] = (uint8_t)(i ^ 0xA5u);
    reply_upload_file(ctx, payload, sizeof(payload));
}

/* Two-chunk download: first call returns exactly CHUNK_SIZE (128 KiB)
 * of data (signals "more to come"), second call returns a short chunk
 * (signals EOF). */
static void on_get_file_two_chunks(MtRpcContext *ctx) {
    g_get_file_calls++;
    if (g_get_file_calls == 1) {
        /* Full chunk — 128 KiB of deterministic data. */
        uint8_t *full = (uint8_t *)malloc(128 * 1024);
        if (!full) return;
        for (size_t i = 0; i < 128 * 1024; ++i) full[i] = (uint8_t)(i & 0xFFu);
        reply_upload_file(ctx, full, 128 * 1024);
        free(full);
    } else {
        uint8_t tail[64];
        memset(tail, 0x5A, sizeof(tail));
        reply_upload_file(ctx, tail, sizeof(tail));
    }
}

/* upload.getFile always returns FILE_MIGRATE_3. */
static void on_get_file_migrate(MtRpcContext *ctx) {
    g_get_file_calls++;
    mt_server_reply_error(ctx, 303, "FILE_MIGRATE_3");
}

/* ================================================================ */
/* Tests                                                            */
/* ================================================================ */

static void test_upload_small_document(void) {
    with_tmp_home("up-small");
    mt_server_init(); mt_server_reset();
    reset_counters();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_upload_saveFilePart, on_save_file_part, NULL);
    mt_server_expect(CRC_messages_sendMedia,  on_send_media,     NULL);

    /* 1 KiB file — one saveFilePart call. */
    const char *path = make_tempfile("up-small", 1024);
    ASSERT(path != NULL, "tempfile created");

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer self = { .kind = HISTORY_PEER_SELF };
    RpcError err = {0};
    ASSERT(domain_send_file(&cfg, &s, &t, &self, path,
                            "a tiny file", "text/plain", &err) == 0,
           "domain_send_file ok");
    ASSERT(g_save_file_part_calls == 1, "exactly one saveFilePart call");

    unlink(path);
    transport_close(&t);
    mt_server_reset();
}

static void test_upload_multi_chunk_document(void) {
    with_tmp_home("up-multi");
    mt_server_init(); mt_server_reset();
    reset_counters();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_upload_saveFilePart, on_save_file_part, NULL);
    mt_server_expect(CRC_messages_sendMedia,  on_send_media,     NULL);

    /* 513 KiB → 2 saveFilePart calls (chunk = 512 KiB). */
    const char *path = make_tempfile("up-multi", 513 * 1024);
    ASSERT(path != NULL, "tempfile created");

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer self = { .kind = HISTORY_PEER_SELF };
    RpcError err = {0};
    ASSERT(domain_send_file(&cfg, &s, &t, &self, path,
                            NULL, NULL, &err) == 0,
           "send_file 513 KiB ok");
    ASSERT(g_save_file_part_calls == 2, "two saveFilePart calls");

    unlink(path);
    transport_close(&t);
    mt_server_reset();
}

static void test_upload_big_file_uses_big_part(void) {
    with_tmp_home("up-big");
    mt_server_init(); mt_server_reset();
    reset_counters();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_upload_saveBigFilePart, on_save_big_file_part, NULL);
    mt_server_expect(CRC_messages_sendMedia,     on_send_media,         NULL);

    /* UPLOAD_BIG_THRESHOLD = 10 MiB — go just past it. */
    size_t sz = (size_t)UPLOAD_BIG_THRESHOLD + 1024;
    const char *path = make_tempfile("up-big", sz);
    ASSERT(path != NULL, "tempfile created");

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer self = { .kind = HISTORY_PEER_SELF };
    RpcError err = {0};
    ASSERT(domain_send_file(&cfg, &s, &t, &self, path,
                            NULL, NULL, &err) == 0,
           "send_file >=10 MiB ok");
    /* 10 MiB + 1 KiB ÷ 512 KiB chunk = 21 parts. */
    ASSERT(g_save_big_file_part_calls >= 21,
           ">= 21 saveBigFilePart calls");
    ASSERT(g_save_file_part_calls == 0, "no small saveFilePart for big file");

    unlink(path);
    transport_close(&t);
    mt_server_reset();
}

static void test_upload_photo_uses_saveFilePart(void) {
    with_tmp_home("up-photo");
    mt_server_init(); mt_server_reset();
    reset_counters();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_upload_saveFilePart, on_save_file_part, NULL);
    mt_server_expect(CRC_messages_sendMedia,  on_send_media,     NULL);

    const char *path = make_tempfile("up-photo", 2048);
    ASSERT(path != NULL, "tempfile created");

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    HistoryPeer self = { .kind = HISTORY_PEER_SELF };
    RpcError err = {0};
    ASSERT(domain_send_photo(&cfg, &s, &t, &self, path, "pic", &err) == 0,
           "send_photo ok");
    ASSERT(g_save_file_part_calls == 1, "photo uploaded as one small part");

    unlink(path);
    transport_close(&t);
    mt_server_reset();
}

/* Populate a MediaInfo struct with values the downloader expects. */
static void make_media_info(MediaInfo *mi) {
    memset(mi, 0, sizeof(*mi));
    mi->kind = MEDIA_PHOTO;
    mi->photo_id = 0xDEADBEEFLL;
    mi->access_hash = 0xCAFEBABELL;
    mi->dc_id = 2;
    mi->file_reference_len = 4;
    mi->file_reference[0] = 0x11;
    mi->file_reference[1] = 0x22;
    mi->file_reference[2] = 0x33;
    mi->file_reference[3] = 0x44;
    strcpy(mi->thumb_type, "y");
}

static void test_download_photo_short_chunk(void) {
    with_tmp_home("dl-short");
    mt_server_init(); mt_server_reset();
    reset_counters();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_upload_getFile, on_get_file_short, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    MediaInfo mi; make_media_info(&mi);
    const char *out = "/tmp/tg-cli-ft-media-dl-short.bin";
    int wrong = -1;
    ASSERT(domain_download_photo(&cfg, &s, &t, &mi, out, &wrong) == 0,
           "download ok");
    ASSERT(wrong == 0, "no wrong_dc");
    ASSERT(g_get_file_calls == 1,
           "single call — EOF from short chunk on first try");

    struct stat st;
    ASSERT(stat(out, &st) == 0, "output exists");
    ASSERT(st.st_size == 128, "128 bytes written");

    unlink(out);
    transport_close(&t);
    mt_server_reset();
}

static void test_download_photo_two_chunks(void) {
    with_tmp_home("dl-two");
    mt_server_init(); mt_server_reset();
    reset_counters();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_upload_getFile, on_get_file_two_chunks, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    MediaInfo mi; make_media_info(&mi);
    const char *out = "/tmp/tg-cli-ft-media-dl-two.bin";
    int wrong = -1;
    ASSERT(domain_download_photo(&cfg, &s, &t, &mi, out, &wrong) == 0,
           "download ok");
    ASSERT(wrong == 0, "no wrong_dc");
    ASSERT(g_get_file_calls == 2,
           "two calls: first full chunk, second EOF");

    struct stat st;
    ASSERT(stat(out, &st) == 0, "output exists");
    ASSERT(st.st_size == 128 * 1024 + 64, "128 KiB + 64 bytes written");

    unlink(out);
    transport_close(&t);
    mt_server_reset();
}

static void test_download_photo_file_migrate(void) {
    with_tmp_home("dl-mig");
    mt_server_init(); mt_server_reset();
    reset_counters();
    MtProtoSession s; load_session(&s);
    mt_server_expect(CRC_upload_getFile, on_get_file_migrate, NULL);

    ApiConfig cfg; init_cfg(&cfg);
    Transport t; connect_mock(&t);

    MediaInfo mi; make_media_info(&mi);
    const char *out = "/tmp/tg-cli-ft-media-dl-mig.bin";
    int wrong = -1;
    ASSERT(domain_download_photo(&cfg, &s, &t, &mi, out, &wrong) == -1,
           "download fails with migrate");
    ASSERT(wrong == 3, "wrong_dc surfaced as 3");

    unlink(out);
    transport_close(&t);
    mt_server_reset();
}

static void test_path_is_image(void) {
    /* Pure helper — no server — but lives here for coupling with the
     * upload module. */
    ASSERT(domain_path_is_image("foo.jpg"),  "jpg → image");
    ASSERT(domain_path_is_image("foo.PNG"),  "png → image (case)");
    ASSERT(domain_path_is_image("a/b.webp"), "webp → image");
    ASSERT(!domain_path_is_image("x.txt"),   "txt → not image");
    ASSERT(!domain_path_is_image("x"),       "no dot → not image");
    ASSERT(!domain_path_is_image(NULL),      "NULL → not image");
}

void run_upload_download_tests(void) {
    RUN_TEST(test_upload_small_document);
    RUN_TEST(test_upload_multi_chunk_document);
    RUN_TEST(test_upload_big_file_uses_big_part);
    RUN_TEST(test_upload_photo_uses_saveFilePart);
    RUN_TEST(test_download_photo_short_chunk);
    RUN_TEST(test_download_photo_two_chunks);
    RUN_TEST(test_download_photo_file_migrate);
    RUN_TEST(test_path_is_image);
}
