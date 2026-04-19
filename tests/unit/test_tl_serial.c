/**
 * @file test_tl_serial.c
 * @brief Unit tests for TL binary serialization.
 */

#include "test_helpers.h"
#include "tl_serial.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Forward declarations of individual test functions */
void test_tl_writer_init_free(void);
void test_tl_write_int32(void);
void test_tl_write_int32_negative(void);
void test_tl_write_uint32(void);
void test_tl_write_int64(void);
void test_tl_write_uint64(void);
void test_tl_write_int128(void);
void test_tl_write_int256(void);
void test_tl_write_double(void);
void test_tl_write_bool_true(void);
void test_tl_write_bool_false(void);
void test_tl_write_string_short(void);
void test_tl_write_string_empty(void);
void test_tl_write_string_needs_padding(void);
void test_tl_write_string_aligned(void);
void test_tl_write_bytes_long(void);
void test_tl_write_vector_begin(void);
void test_tl_write_multiple(void);
void test_tl_reader_init(void);
void test_tl_reader_ok(void);
void test_tl_read_int32(void);
void test_tl_read_uint32(void);
void test_tl_read_int64(void);
void test_tl_read_int128(void);
void test_tl_read_int256(void);
void test_tl_read_bool_true(void);
void test_tl_read_bool_false(void);
void test_tl_read_bool_invalid(void);
void test_tl_read_string_short(void);
void test_tl_read_string_empty(void);
void test_tl_read_bytes_with_padding(void);
void test_tl_read_bytes_empty(void);
void test_tl_read_bytes_long_prefix(void);
void test_tl_read_past_end(void);
void test_tl_read_skip(void);
void test_tl_read_raw(void);
void test_tl_roundtrip_int32(void);
void test_tl_roundtrip_uint64(void);
void test_tl_roundtrip_string(void);
void test_tl_roundtrip_int128_int256(void);
void test_tl_roundtrip_double(void);
void test_tl_roundtrip_bool(void);
void test_tl_roundtrip_mixed(void);
void test_tl_string_null(void);
void test_tl_writer_grow(void);
void test_tl_vector_overflow_uint32(void);
void test_tl_vector_overflow_int32(void);
void test_tl_vector_overflow_uint64(void);
void test_tl_vector_overflow_string(void);
void test_tl_vector_overflow_reader_saturates(void);

/* ================================================================
 * Writer tests
 * ================================================================ */

void test_tl_writer_init_free(void) {
    TlWriter w;
    tl_writer_init(&w);
    ASSERT(w.data == NULL, "data should be NULL after init");
    ASSERT(w.len == 0, "len should be 0 after init");
    ASSERT(w.cap == 0, "cap should be 0 after init");
    tl_writer_free(&w);
}

void test_tl_write_int32(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_int32(&w, 0x01020304);
    ASSERT(w.len == 4, "should have 4 bytes");
    /* Little-endian: 04 03 02 01 */
    ASSERT(w.data[0] == 0x04, "byte 0 should be 0x04");
    ASSERT(w.data[1] == 0x03, "byte 1 should be 0x03");
    ASSERT(w.data[2] == 0x02, "byte 2 should be 0x02");
    ASSERT(w.data[3] == 0x01, "byte 3 should be 0x01");
    tl_writer_free(&w);
}

void test_tl_write_int32_negative(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_int32(&w, -1);
    ASSERT(w.len == 4, "should have 4 bytes");
    ASSERT(w.data[0] == 0xFF && w.data[1] == 0xFF
        && w.data[2] == 0xFF && w.data[3] == 0xFF,
           "-1 should be 0xFFFFFFFF in LE");
    tl_writer_free(&w);
}

void test_tl_write_uint32(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint32(&w, 0xABCDEF01u);
    ASSERT(w.len == 4, "should have 4 bytes");
    ASSERT(w.data[0] == 0x01, "byte 0 LE");
    ASSERT(w.data[3] == 0xAB, "byte 3 LE");
    tl_writer_free(&w);
}

void test_tl_write_int64(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_int64(&w, 0x0102030405060708LL);
    ASSERT(w.len == 8, "should have 8 bytes");
    ASSERT(w.data[0] == 0x08, "byte 0 LE");
    ASSERT(w.data[7] == 0x01, "byte 7 LE");
    tl_writer_free(&w);
}

void test_tl_write_uint64(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint64(&w, 0xFEDCBA9876543210ULL);
    ASSERT(w.len == 8, "should have 8 bytes");
    ASSERT(w.data[0] == 0x10, "byte 0 LE");
    ASSERT(w.data[7] == 0xFE, "byte 7 LE");
    tl_writer_free(&w);
}

void test_tl_write_int128(void) {
    TlWriter w;
    tl_writer_init(&w);
    unsigned char val[16];
    for (int i = 0; i < 16; i++) val[i] = (unsigned char)(i + 1);
    tl_write_int128(&w, val);
    ASSERT(w.len == 16, "should have 16 bytes");
    ASSERT(w.data[0] == 1, "first byte should be 1");
    ASSERT(w.data[15] == 16, "last byte should be 16");
    tl_writer_free(&w);
}

void test_tl_write_int256(void) {
    TlWriter w;
    tl_writer_init(&w);
    unsigned char val[32];
    for (int i = 0; i < 32; i++) val[i] = (unsigned char)i;
    tl_write_int256(&w, val);
    ASSERT(w.len == 32, "should have 32 bytes");
    ASSERT(w.data[0] == 0, "first byte should be 0");
    ASSERT(w.data[31] == 31, "last byte should be 31");
    tl_writer_free(&w);
}

void test_tl_write_double(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_double(&w, 3.14);
    ASSERT(w.len == 8, "should have 8 bytes");
    /* Verify by reading back */
    TlReader r = tl_reader_init(w.data, w.len);
    double val = tl_read_double(&r);
    ASSERT(fabs(val - 3.14) < 1e-10, "round-trip should preserve value");
    tl_writer_free(&w);
}

void test_tl_write_bool_true(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_bool(&w, 1);
    ASSERT(w.len == 4, "should have 4 bytes");
    /* 0x997275b5 in LE */
    ASSERT(w.data[0] == 0xB5, "bool true magic byte 0");
    ASSERT(w.data[1] == 0x75, "bool true magic byte 1");
    ASSERT(w.data[2] == 0x72, "bool true magic byte 2");
    ASSERT(w.data[3] == 0x99, "bool true magic byte 3");
    tl_writer_free(&w);
}

void test_tl_write_bool_false(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_bool(&w, 0);
    ASSERT(w.len == 4, "should have 4 bytes");
    /* 0xbc799737 in LE */
    ASSERT(w.data[0] == 0x37, "bool false magic byte 0");
    ASSERT(w.data[1] == 0x97, "bool false magic byte 1");
    ASSERT(w.data[2] == 0x79, "bool false magic byte 2");
    ASSERT(w.data[3] == 0xBC, "bool false magic byte 3");
    tl_writer_free(&w);
}

void test_tl_write_string_short(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_string(&w, "abc");
    /* 1-byte prefix (3) + 3 bytes data + 0 padding (total 4, aligned) = 4 bytes */
    ASSERT(w.len == 4, "short string should be 4 bytes (1 prefix + 3 data)");
    ASSERT(w.data[0] == 3, "length prefix should be 3");
    ASSERT(w.data[1] == 'a', "first char");
    ASSERT(w.data[2] == 'b', "second char");
    ASSERT(w.data[3] == 'c', "third char");
    tl_writer_free(&w);
}

void test_tl_write_string_empty(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_string(&w, "");
    /* 1-byte prefix (0) + 0 data + 3 padding = 4 bytes */
    ASSERT(w.len == 4, "empty string should be 4 bytes (1 prefix + 3 pad)");
    ASSERT(w.data[0] == 0, "length prefix should be 0");
    tl_writer_free(&w);
}

void test_tl_write_string_needs_padding(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_string(&w, "a");
    /* 1-byte prefix (1) + 1 byte data + 2 padding = 4 bytes */
    ASSERT(w.len == 4, "1-char string should be 4 bytes (1 prefix + 1 data + 2 pad)");
    ASSERT(w.data[0] == 1, "length prefix should be 1");
    ASSERT(w.data[1] == 'a', "char data");
    ASSERT(w.data[2] == 0, "pad byte 1");
    ASSERT(w.data[3] == 0, "pad byte 2");
    tl_writer_free(&w);
}

void test_tl_write_string_aligned(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_string(&w, "abcde");
    /* 1-byte prefix (5) + 5 bytes data + 2 padding = 8 bytes */
    ASSERT(w.len == 8, "5-char string should be 8 bytes");
    ASSERT(w.data[0] == 5, "length prefix should be 5");
    tl_writer_free(&w);
}

void test_tl_write_bytes_long(void) {
    TlWriter w;
    tl_writer_init(&w);
    /* 256 bytes — needs 4-byte length prefix (>= 254) */
    unsigned char data[256];
    memset(data, 0xAB, sizeof(data));
    tl_write_bytes(&w, data, 256);
    /* 4-byte prefix + 256 data + 0 padding (260 is 4-aligned) = 260 */
    ASSERT(w.len == 260, "256-byte data should be 260 bytes");
    ASSERT(w.data[0] == 0xFE, "first byte signals long prefix");
    ASSERT(w.data[1] == 0x00 && w.data[2] == 0x01 && w.data[3] == 0x00,
           "3-byte length should be 256 (LE: 00 01 00)");
    ASSERT(w.data[4] == 0xAB, "data starts at offset 4");
    tl_writer_free(&w);
}

void test_tl_write_vector_begin(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_vector_begin(&w, 3);
    ASSERT(w.len == 8, "vector header should be 8 bytes (ctor + count)");
    /* Constructor 0x1cb5c415 in LE */
    ASSERT(w.data[0] == 0x15, "vector ctor byte 0");
    ASSERT(w.data[4] == 3, "count should be 3");
    tl_writer_free(&w);
}

void test_tl_write_multiple(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_int32(&w, 1);
    tl_write_int32(&w, 2);
    tl_write_int32(&w, 3);
    ASSERT(w.len == 12, "three int32s should be 12 bytes");
    tl_writer_free(&w);
}

/* ================================================================
 * Reader tests
 * ================================================================ */

void test_tl_reader_init(void) {
    unsigned char buf[4] = {0};
    TlReader r = tl_reader_init(buf, 4);
    ASSERT(r.data == buf, "data should point to buffer");
    ASSERT(r.len == 4, "len should be 4");
    ASSERT(r.pos == 0, "pos should be 0");
}

void test_tl_reader_ok(void) {
    unsigned char buf[4] = {1, 2, 3, 4};
    TlReader r = tl_reader_init(buf, 4);
    ASSERT(tl_reader_ok(&r), "should be ok at start");
    r.pos = 4;
    ASSERT(!tl_reader_ok(&r), "should not be ok at end");
}

void test_tl_read_int32(void) {
    unsigned char buf[4] = {0x04, 0x03, 0x02, 0x01};
    TlReader r = tl_reader_init(buf, 4);
    int32_t val = tl_read_int32(&r);
    ASSERT(val == 0x01020304, "should read LE int32");
    ASSERT(r.pos == 4, "pos should advance by 4");
}

void test_tl_read_uint32(void) {
    unsigned char buf[4] = {0x01, 0xEF, 0xCD, 0xAB};
    TlReader r = tl_reader_init(buf, 4);
    uint32_t val = tl_read_uint32(&r);
    ASSERT(val == 0xABCDEF01u, "should read LE uint32");
}

void test_tl_read_int64(void) {
    unsigned char buf[8] = {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
    TlReader r = tl_reader_init(buf, 8);
    int64_t val = tl_read_int64(&r);
    ASSERT(val == 0x0102030405060708LL, "should read LE int64");
}

void test_tl_read_int128(void) {
    unsigned char buf[16];
    for (int i = 0; i < 16; i++) buf[i] = (unsigned char)(i + 10);
    TlReader r = tl_reader_init(buf, 16);
    unsigned char out[16] = {0};
    tl_read_int128(&r, out);
    ASSERT(memcmp(out, buf, 16) == 0, "should read 16 raw bytes");
    ASSERT(r.pos == 16, "pos should advance by 16");
}

void test_tl_read_int256(void) {
    unsigned char buf[32];
    for (int i = 0; i < 32; i++) buf[i] = (unsigned char)(i + 5);
    TlReader r = tl_reader_init(buf, 32);
    unsigned char out[32] = {0};
    tl_read_int256(&r, out);
    ASSERT(memcmp(out, buf, 32) == 0, "should read 32 raw bytes");
}

void test_tl_read_bool_true(void) {
    unsigned char buf[4] = {0xB5, 0x75, 0x72, 0x99};
    TlReader r = tl_reader_init(buf, 4);
    ASSERT(tl_read_bool(&r) == 1, "should read bool true");
}

void test_tl_read_bool_false(void) {
    unsigned char buf[4] = {0x37, 0x97, 0x79, 0xBC};
    TlReader r = tl_reader_init(buf, 4);
    ASSERT(tl_read_bool(&r) == 0, "should read bool false");
}

void test_tl_read_bool_invalid(void) {
    unsigned char buf[4] = {0x00, 0x00, 0x00, 0x00};
    TlReader r = tl_reader_init(buf, 4);
    ASSERT(tl_read_bool(&r) == -1, "should return -1 for invalid bool magic");
}

void test_tl_read_string_short(void) {
    /* 1-byte prefix (3) + "abc" */
    unsigned char buf[] = {3, 'a', 'b', 'c'};
    TlReader r = tl_reader_init(buf, sizeof(buf));
    char *s = tl_read_string(&r);
    ASSERT(s != NULL, "string should not be NULL");
    ASSERT(strcmp(s, "abc") == 0, "should read 'abc'");
    ASSERT(r.pos == 4, "pos should advance past padded area");
    free(s);
}

void test_tl_read_string_empty(void) {
    /* 1-byte prefix (0) + 3 padding */
    unsigned char buf[] = {0, 0, 0, 0};
    TlReader r = tl_reader_init(buf, sizeof(buf));
    char *s = tl_read_string(&r);
    ASSERT(s != NULL, "empty string should not be NULL");
    ASSERT(s[0] == '\0', "empty string should be empty");
    ASSERT(r.pos == 4, "pos should advance past padding");
    free(s);
}

void test_tl_read_bytes_with_padding(void) {
    /* 1-byte prefix (5) + "abcde" + 2 pad bytes */
    unsigned char buf[] = {5, 'a', 'b', 'c', 'd', 'e', 0, 0};
    TlReader r = tl_reader_init(buf, sizeof(buf));
    size_t len = 0;
    unsigned char *b = tl_read_bytes(&r, &len);
    ASSERT(b != NULL, "bytes should not be NULL");
    ASSERT(len == 5, "length should be 5");
    ASSERT(memcmp(b, "abcde", 5) == 0, "content should be 'abcde'");
    ASSERT(r.pos == 8, "pos should advance past padding");
    free(b);
}

void test_tl_read_bytes_empty(void) {
    /* Zero-length bytes: 1-byte prefix (0) + 3 pad bytes.
     * Regression: QA-21 — tl_read_bytes must return a non-NULL pointer for
     * empty payloads even on platforms where malloc(0) returns NULL. */
    TlWriter w;
    tl_writer_init(&w);
    tl_write_bytes(&w, NULL, 0);
    ASSERT(w.len == 4, "empty bytes should serialize to 4 bytes");

    TlReader r = tl_reader_init(w.data, w.len);
    size_t len = 42;
    unsigned char *b = tl_read_bytes(&r, &len);
    ASSERT(b != NULL, "empty bytes should return non-NULL pointer");
    ASSERT(len == 0, "length should be 0");
    ASSERT(r.pos == 4, "pos should advance past padding");
    free(b);
    tl_writer_free(&w);
}

void test_tl_read_bytes_long_prefix(void) {
    /* 4-byte prefix: 0xFE, 0x00, 0x01, 0x00 = 256 bytes */
    size_t total = 4 + 256;
    unsigned char *buf = (unsigned char *)calloc(1, total);
    buf[0] = 0xFE;
    buf[1] = 0x00;
    buf[2] = 0x01;
    buf[3] = 0x00;
    memset(buf + 4, 'X', 256);

    TlReader r = tl_reader_init(buf, total);
    size_t len = 0;
    unsigned char *b = tl_read_bytes(&r, &len);
    ASSERT(b != NULL, "long bytes should not be NULL");
    ASSERT(len == 256, "length should be 256");
    ASSERT(b[0] == 'X' && b[255] == 'X', "content should be all X");
    ASSERT(r.pos == total, "pos should advance to end");
    free(b);
    free(buf);
}

void test_tl_read_past_end(void) {
    unsigned char buf[2] = {1, 2};
    TlReader r = tl_reader_init(buf, 2);
    int32_t val = tl_read_int32(&r);
    ASSERT(val == 0, "reading past end should return 0");
    ASSERT(r.pos == r.len, "pos should be at end");
    ASSERT(!tl_reader_ok(&r), "reader should not be ok");
}

void test_tl_read_skip(void) {
    unsigned char buf[8] = {0};
    TlReader r = tl_reader_init(buf, 8);
    tl_read_skip(&r, 5);
    ASSERT(r.pos == 5, "pos should advance by 5");
    tl_read_skip(&r, 10);
    ASSERT(r.pos == 8, "pos should clamp to len");
}

void test_tl_read_raw(void) {
    unsigned char buf[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    TlReader r = tl_reader_init(buf, 4);
    unsigned char out[4] = {0};
    tl_read_raw(&r, out, 4);
    ASSERT(memcmp(out, buf, 4) == 0, "raw read should copy bytes exactly");
}

/* ================================================================
 * Round-trip tests
 * ================================================================ */

void test_tl_roundtrip_int32(void) {
    TlWriter w;
    tl_writer_init(&w);
    int32_t values[] = {0, 1, -1, 0x7FFFFFFF, (int32_t)0x80000000};
    for (int i = 0; i < 5; i++) tl_write_int32(&w, values[i]);

    TlReader r = tl_reader_init(w.data, w.len);
    for (int i = 0; i < 5; i++) {
        ASSERT(tl_read_int32(&r) == values[i], "round-trip int32 mismatch");
    }
    ASSERT(!tl_reader_ok(&r), "reader should be at end");
    tl_writer_free(&w);
}

void test_tl_roundtrip_uint64(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_uint64(&w, 0);
    tl_write_uint64(&w, 0xFFFFFFFFFFFFFFFFULL);

    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_read_uint64(&r) == 0, "round-trip uint64 zero");
    ASSERT(tl_read_uint64(&r) == 0xFFFFFFFFFFFFFFFFULL, "round-trip uint64 max");
    tl_writer_free(&w);
}

void test_tl_roundtrip_string(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_string(&w, "");
    tl_write_string(&w, "hello");
    tl_write_string(&w, "a longer string for testing purposes");

    TlReader r = tl_reader_init(w.data, w.len);
    char *s;

    s = tl_read_string(&r);
    ASSERT(s != NULL && strcmp(s, "") == 0, "round-trip empty string");
    free(s);

    s = tl_read_string(&r);
    ASSERT(s != NULL && strcmp(s, "hello") == 0, "round-trip 'hello'");
    free(s);

    s = tl_read_string(&r);
    ASSERT(s != NULL && strcmp(s, "a longer string for testing purposes") == 0,
           "round-trip longer string");
    free(s);

    ASSERT(!tl_reader_ok(&r), "reader should be at end");
    tl_writer_free(&w);
}

void test_tl_roundtrip_int128_int256(void) {
    TlWriter w;
    tl_writer_init(&w);
    unsigned char v128[16], v256[32];
    for (int i = 0; i < 16; i++) v128[i] = (unsigned char)(i * 17);
    for (int i = 0; i < 32; i++) v256[i] = (unsigned char)(i * 13);

    tl_write_int128(&w, v128);
    tl_write_int256(&w, v256);

    TlReader r = tl_reader_init(w.data, w.len);
    unsigned char o128[16], o256[32];
    tl_read_int128(&r, o128);
    tl_read_int256(&r, o256);
    ASSERT(memcmp(o128, v128, 16) == 0, "round-trip int128");
    ASSERT(memcmp(o256, v256, 32) == 0, "round-trip int256");
    tl_writer_free(&w);
}

void test_tl_roundtrip_double(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_double(&w, 0.0);
    tl_write_double(&w, -1.5);
    tl_write_double(&w, 1e20);

    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_read_double(&r) == 0.0, "round-trip double 0.0");
    ASSERT(tl_read_double(&r) == -1.5, "round-trip double -1.5");
    ASSERT(fabs(tl_read_double(&r) - 1e20) < 1e10, "round-trip double 1e20");
    tl_writer_free(&w);
}

void test_tl_roundtrip_bool(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_bool(&w, 0);
    tl_write_bool(&w, 1);
    tl_write_bool(&w, 42); /* non-zero = true */

    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_read_bool(&r) == 0, "round-trip bool false");
    ASSERT(tl_read_bool(&r) == 1, "round-trip bool true");
    ASSERT(tl_read_bool(&r) == 1, "round-trip bool 42→true");
    tl_writer_free(&w);
}

void test_tl_roundtrip_mixed(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_int32(&w, 0x12345678);
    tl_write_string(&w, "test");
    tl_write_bool(&w, 1);
    tl_write_uint64(&w, 999);

    TlReader r = tl_reader_init(w.data, w.len);
    ASSERT(tl_read_int32(&r) == 0x12345678, "mixed: int32");
    char *s = tl_read_string(&r);
    ASSERT(s != NULL && strcmp(s, "test") == 0, "mixed: string");
    free(s);
    ASSERT(tl_read_bool(&r) == 1, "mixed: bool");
    ASSERT(tl_read_uint64(&r) == 999, "mixed: uint64");
    ASSERT(!tl_reader_ok(&r), "mixed: reader at end");
    tl_writer_free(&w);
}

void test_tl_string_null(void) {
    TlWriter w;
    tl_writer_init(&w);
    tl_write_string(&w, NULL);
    /* Should behave like empty string */
    ASSERT(w.len == 4, "NULL string should be treated as empty (4 bytes)");
    ASSERT(w.data[0] == 0, "length prefix should be 0");
    tl_writer_free(&w);
}

void test_tl_writer_grow(void) {
    TlWriter w;
    tl_writer_init(&w);
    /* Write 1000 int32s — forces multiple reallocations */
    for (int i = 0; i < 1000; i++) {
        tl_write_int32(&w, i);
    }
    ASSERT(w.len == 4000, "1000 int32s should be 4000 bytes");
    TlReader r = tl_reader_init(w.data, w.len);
    for (int i = 0; i < 1000; i++) {
        ASSERT(tl_read_int32(&r) == i, "large round-trip mismatch");
    }
    tl_writer_free(&w);
}

/* ================================================================
 * Vector overflow / fuzz-style tests (TEST-30)
 *
 * Each test builds a TL vector header with count=0x7FFFFFFF followed by
 * only a few bytes of actual element data.  The saturating TlReader must
 * bound all reads to the buffer: no OOB access, no infinite spin.
 * ================================================================ */

/**
 * @brief Helper: build a buffer whose first 8 bytes are a TL vector header
 * (constructor 0x1cb5c415 + count) followed by @p extra bytes of payload.
 */
static void build_vector_buf(unsigned char *buf, size_t buf_len,
                              uint32_t count, const unsigned char *extra,
                              size_t extra_len)
{
    /* vector constructor LE */
    buf[0] = 0x15; buf[1] = 0xc4; buf[2] = 0xb5; buf[3] = 0x1c;
    /* count LE */
    buf[4] = (unsigned char)(count);
    buf[5] = (unsigned char)(count >> 8);
    buf[6] = (unsigned char)(count >> 16);
    buf[7] = (unsigned char)(count >> 24);
    for (size_t i = 0; i < extra_len && (8 + i) < buf_len; i++)
        buf[8 + i] = extra[i];
}

/**
 * @brief Vector with count=0x7FFFFFFF, only 4 bytes of uint32 payload.
 * Reading all claimed elements via tl_read_uint32 must not crash and
 * the reader must saturate after the real data runs out.
 */
void test_tl_vector_overflow_uint32(void) {
    /* vector header (8) + one real uint32 element (4) = 12 bytes total */
    unsigned char buf[12];
    unsigned char elem[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    build_vector_buf(buf, sizeof(buf), 0x7FFFFFFFu, elem, 4);

    TlReader r = tl_reader_init(buf, sizeof(buf));
    /* consume vector header */
    uint32_t ctor  = tl_read_uint32(&r);
    uint32_t count = tl_read_uint32(&r);
    ASSERT(ctor == 0x1cb5c415u, "vector ctor magic");
    ASSERT(count == 0x7FFFFFFFu, "count should be 0x7FFFFFFF");

    /* iterate — reader saturates after the first real element */
    uint32_t last_val = 0;
    for (uint32_t i = 0; i < count; i++) {
        last_val = tl_read_uint32(&r);
        if (!tl_reader_ok(&r)) break; /* natural termination */
    }
    /* The first element read the real bytes; subsequent reads returned 0 */
    (void)last_val;
    ASSERT(r.pos == r.len, "reader must be saturated at end of buffer");
}

/**
 * @brief Same but elements are int32 — loop terminates, no OOB.
 */
void test_tl_vector_overflow_int32(void) {
    unsigned char buf[12];
    unsigned char elem[4] = {0x01, 0x02, 0x03, 0x04};
    build_vector_buf(buf, sizeof(buf), 0x7FFFFFFFu, elem, 4);

    TlReader r = tl_reader_init(buf, sizeof(buf));
    tl_read_uint32(&r); /* ctor */
    uint32_t count = tl_read_uint32(&r);
    ASSERT(count == 0x7FFFFFFFu, "count should be 0x7FFFFFFF");

    for (uint32_t i = 0; i < count; i++) {
        tl_read_int32(&r);
        if (!tl_reader_ok(&r)) break;
    }
    ASSERT(r.pos == r.len, "reader saturated after overflow int32 loop");
}

/**
 * @brief Vector whose claimed elements are uint64 (8 bytes each).
 * Only 8 bytes of real payload follow the header — reader saturates after one.
 */
void test_tl_vector_overflow_uint64(void) {
    unsigned char buf[16]; /* header(8) + one uint64(8) */
    unsigned char elem[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    build_vector_buf(buf, sizeof(buf), 0x7FFFFFFFu, elem, 8);

    TlReader r = tl_reader_init(buf, sizeof(buf));
    tl_read_uint32(&r); /* ctor */
    uint32_t count = tl_read_uint32(&r);
    ASSERT(count == 0x7FFFFFFFu, "count should be 0x7FFFFFFF");

    for (uint32_t i = 0; i < count; i++) {
        tl_read_uint64(&r);
        if (!tl_reader_ok(&r)) break;
    }
    ASSERT(r.pos == r.len, "reader saturated after overflow uint64 loop");
}

/**
 * @brief Vector whose elements are TL strings — only 4 bytes follow the header.
 * tl_read_string / tl_read_bytes must return NULL once the buffer is exhausted.
 */
void test_tl_vector_overflow_string(void) {
    /* header(8) + short string "hi" padded to 4 bytes = 12 bytes */
    unsigned char buf[12] = {
        0x15, 0xc4, 0xb5, 0x1c,  /* vector ctor */
        0xFF, 0xFF, 0xFF, 0x7F,  /* count = 0x7FFFFFFF LE */
        0x02, 'h',  'i',  0x00   /* TL string: len=2, "hi", 1 pad byte */
    };

    TlReader r = tl_reader_init(buf, sizeof(buf));
    tl_read_uint32(&r); /* ctor */
    uint32_t count = tl_read_uint32(&r);
    ASSERT(count == 0x7FFFFFFFu, "count should be 0x7FFFFFFF");

    int null_seen = 0;
    for (uint32_t i = 0; i < count; i++) {
        char *s = tl_read_string(&r);
        if (s == NULL) { null_seen = 1; break; }
        free(s);
        if (!tl_reader_ok(&r)) { null_seen = 1; break; }
    }
    ASSERT(null_seen, "tl_read_string must return NULL when buffer exhausted");
    ASSERT(r.pos == r.len, "reader saturated after overflow string loop");
}

/**
 * @brief Minimal buffer: only the vector header, no element data.
 * The very first element read on a saturated reader must return 0 / NULL,
 * and pos must stay clamped to len throughout.
 */
void test_tl_vector_overflow_reader_saturates(void) {
    /* Only 8 bytes: the vector header, no element data at all */
    unsigned char buf[8];
    build_vector_buf(buf, sizeof(buf), 0x7FFFFFFFu, NULL, 0);

    TlReader r = tl_reader_init(buf, sizeof(buf));
    tl_read_uint32(&r); /* ctor */
    uint32_t count = tl_read_uint32(&r);
    ASSERT(count == 0x7FFFFFFFu, "count should be 0x7FFFFFFF");
    ASSERT(r.pos == r.len, "reader exhausted after consuming header");

    /* Subsequent reads on exhausted reader must be safe */
    uint32_t v = tl_read_uint32(&r);
    ASSERT(v == 0, "read on exhausted reader returns 0");
    ASSERT(r.pos == r.len, "pos stays clamped at len");

    int64_t v64 = tl_read_int64(&r);
    ASSERT(v64 == 0, "int64 read on exhausted reader returns 0");
    ASSERT(r.pos == r.len, "pos still clamped");
}

/* ================================================================
 * Test suite entry point
 * ================================================================ */

void test_tl_serial(void) {
    RUN_TEST(test_tl_writer_init_free);
    RUN_TEST(test_tl_write_int32);
    RUN_TEST(test_tl_write_int32_negative);
    RUN_TEST(test_tl_write_uint32);
    RUN_TEST(test_tl_write_int64);
    RUN_TEST(test_tl_write_uint64);
    RUN_TEST(test_tl_write_int128);
    RUN_TEST(test_tl_write_int256);
    RUN_TEST(test_tl_write_double);
    RUN_TEST(test_tl_write_bool_true);
    RUN_TEST(test_tl_write_bool_false);
    RUN_TEST(test_tl_write_string_short);
    RUN_TEST(test_tl_write_string_empty);
    RUN_TEST(test_tl_write_string_needs_padding);
    RUN_TEST(test_tl_write_string_aligned);
    RUN_TEST(test_tl_write_bytes_long);
    RUN_TEST(test_tl_write_vector_begin);
    RUN_TEST(test_tl_write_multiple);
    RUN_TEST(test_tl_reader_init);
    RUN_TEST(test_tl_reader_ok);
    RUN_TEST(test_tl_read_int32);
    RUN_TEST(test_tl_read_uint32);
    RUN_TEST(test_tl_read_int64);
    RUN_TEST(test_tl_read_int128);
    RUN_TEST(test_tl_read_int256);
    RUN_TEST(test_tl_read_bool_true);
    RUN_TEST(test_tl_read_bool_false);
    RUN_TEST(test_tl_read_bool_invalid);
    RUN_TEST(test_tl_read_string_short);
    RUN_TEST(test_tl_read_string_empty);
    RUN_TEST(test_tl_read_bytes_with_padding);
    RUN_TEST(test_tl_read_bytes_empty);
    RUN_TEST(test_tl_read_bytes_long_prefix);
    RUN_TEST(test_tl_read_past_end);
    RUN_TEST(test_tl_read_skip);
    RUN_TEST(test_tl_read_raw);
    RUN_TEST(test_tl_roundtrip_int32);
    RUN_TEST(test_tl_roundtrip_uint64);
    RUN_TEST(test_tl_roundtrip_string);
    RUN_TEST(test_tl_roundtrip_int128_int256);
    RUN_TEST(test_tl_roundtrip_double);
    RUN_TEST(test_tl_roundtrip_bool);
    RUN_TEST(test_tl_roundtrip_mixed);
    RUN_TEST(test_tl_string_null);
    RUN_TEST(test_tl_writer_grow);
    RUN_TEST(test_tl_vector_overflow_uint32);
    RUN_TEST(test_tl_vector_overflow_int32);
    RUN_TEST(test_tl_vector_overflow_uint64);
    RUN_TEST(test_tl_vector_overflow_string);
    RUN_TEST(test_tl_vector_overflow_reader_saturates);
}
