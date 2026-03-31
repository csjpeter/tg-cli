/**
 * @file test_gzip.c
 * @brief Unit tests for gzip_packed unwrapping (rpc_unwrap_gzip).
 *
 * Tests both the gzip decompression path (using real gzip data)
 * and the pass-through path (non-gzip data copied unchanged).
 */

#include "test_helpers.h"
#include "mtproto_rpc.h"
#include "tl_serial.h"
#include "tinf.h"

#include <string.h>
#include <stdlib.h>

/* ---- Pre-compressed gzip test data ----
 *
 * Created by compressing "Hello, MTProto!" (15 bytes) with gzip.
 * Generated via: echo -n "Hello, MTProto!" | gzip | xxd -i
 */
static const uint8_t gzip_hello[] = {
    0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x03, 0xf3, 0x48, 0xcd, 0xc9, 0xc9, 0xd7,
    0x51, 0xf0, 0x0d, 0x09, 0x28, 0xca, 0x2f, 0xc9,
    0x57, 0x04, 0x00, 0x59, 0xf5, 0x5d, 0x03, 0x0f,
    0x00, 0x00, 0x00
};
static const size_t gzip_hello_len = sizeof(gzip_hello);

/* ---- Tests ---- */

void test_unwrap_gzip_passthrough(void) {
    /* Non-gzip data should be copied unchanged */
    uint8_t data[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    uint8_t out[256];
    size_t out_len = 0;

    int rc = rpc_unwrap_gzip(data, sizeof(data), out, sizeof(out), &out_len);
    ASSERT(rc == 0, "passthrough should succeed");
    ASSERT(out_len == sizeof(data), "output length should match input");
    ASSERT(memcmp(out, data, sizeof(data)) == 0, "data should be unchanged");
}

void test_unwrap_gzip_passthrough_constructor(void) {
    /* Data with a different constructor should pass through */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x05162463); /* CRC_resPQ — not gzip_packed */
    tl_write_int32(&w, 42);

    uint8_t out[256];
    size_t out_len = 0;
    int rc = rpc_unwrap_gzip(w.data, w.len, out, sizeof(out), &out_len);
    ASSERT(rc == 0, "non-gzip constructor should pass through");
    ASSERT(out_len == w.len, "output length should match");
    ASSERT(memcmp(out, w.data, w.len) == 0, "data should be unchanged");
    tl_writer_free(&w);
}

void test_unwrap_gzip_decompress(void) {
    /* Build gzip_packed TL: constructor(4) + bytes(gzip_hello) */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x3072cfa1); /* CRC_gzip_packed */
    tl_write_bytes(&w, gzip_hello, gzip_hello_len);

    uint8_t out[1024];
    size_t out_len = 0;
    int rc = rpc_unwrap_gzip(w.data, w.len, out, sizeof(out), &out_len);
    tl_writer_free(&w);

    ASSERT(rc == 0, "gzip decompression should succeed");
    ASSERT(out_len == 15, "decompressed length should be 15");
    ASSERT(memcmp(out, "Hello, MTProto!", 15) == 0,
           "decompressed data should match original");
}

void test_unwrap_gzip_null_args(void) {
    uint8_t buf[16];
    size_t len = 0;
    ASSERT(rpc_unwrap_gzip(NULL, 4, buf, 16, &len) == -1, "NULL data should fail");
    ASSERT(rpc_unwrap_gzip(buf, 4, NULL, 16, &len) == -1, "NULL out should fail");
    ASSERT(rpc_unwrap_gzip(buf, 4, buf, 16, NULL) == -1, "NULL out_len should fail");
}

void test_unwrap_gzip_short_data(void) {
    /* Data shorter than 4 bytes — pass through */
    uint8_t data[] = { 0xAA, 0xBB };
    uint8_t out[16];
    size_t out_len = 0;
    int rc = rpc_unwrap_gzip(data, 2, out, sizeof(out), &out_len);
    ASSERT(rc == 0, "short data should pass through");
    ASSERT(out_len == 2, "output length should be 2");
}

void test_unwrap_gzip_buffer_too_small(void) {
    /* Passthrough with too-small buffer */
    uint8_t data[32];
    memset(data, 0x42, 32);
    uint8_t out[8];
    size_t out_len = 0;
    int rc = rpc_unwrap_gzip(data, 32, out, 8, &out_len);
    ASSERT(rc == -1, "buffer too small should fail");
}

void test_unwrap_gzip_corrupt_data(void) {
    /* gzip_packed with corrupt compressed data */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x3072cfa1); /* CRC_gzip_packed */
    uint8_t garbage[] = { 0xFF, 0xFE, 0xFD, 0xFC, 0xFB };
    tl_write_bytes(&w, garbage, sizeof(garbage));

    uint8_t out[1024];
    size_t out_len = 0;
    int rc = rpc_unwrap_gzip(w.data, w.len, out, sizeof(out), &out_len);
    tl_writer_free(&w);

    ASSERT(rc == -1, "corrupt gzip data should fail");
}

void test_unwrap_gzip_empty_bytes(void) {
    /* gzip_packed with empty bytes field */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, 0x3072cfa1);
    tl_write_bytes(&w, (uint8_t[]){0}, 0); /* empty bytes */

    uint8_t out[1024];
    size_t out_len = 0;
    int rc = rpc_unwrap_gzip(w.data, w.len, out, sizeof(out), &out_len);
    tl_writer_free(&w);

    ASSERT(rc == -1, "empty gzip data should fail");
}

void test_tinf_gzip_direct(void) {
    /* Verify tinf works directly with known data */
    uint8_t out[256];
    unsigned int out_len = sizeof(out);
    int rc = tinf_gzip_uncompress(out, &out_len, gzip_hello, (unsigned int)gzip_hello_len);
    ASSERT(rc == TINF_OK, "tinf_gzip_uncompress should succeed");
    ASSERT(out_len == 15, "decompressed length should be 15");
    ASSERT(memcmp(out, "Hello, MTProto!", 15) == 0, "data should match");
}

/* ---- Test suite entry point ---- */

void test_gzip(void) {
    RUN_TEST(test_unwrap_gzip_passthrough);
    RUN_TEST(test_unwrap_gzip_passthrough_constructor);
    RUN_TEST(test_unwrap_gzip_decompress);
    RUN_TEST(test_unwrap_gzip_null_args);
    RUN_TEST(test_unwrap_gzip_short_data);
    RUN_TEST(test_unwrap_gzip_buffer_too_small);
    RUN_TEST(test_unwrap_gzip_corrupt_data);
    RUN_TEST(test_unwrap_gzip_empty_bytes);
    RUN_TEST(test_tinf_gzip_direct);
}
