/**
 * @file tl_serial.c
 * @brief TL (Type Language) binary serialization implementation.
 */

#include "tl_serial.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- TL bool magic constants ---- */
#define TL_BOOL_TRUE  0x997275b5u
#define TL_BOOL_FALSE 0xbc799737u
#define TL_VECTOR_CTOR 0x1cb5c415u

/* ---- Writer helpers ---- */

static void writer_ensure(TlWriter *w, size_t needed) {
    if (w->len + needed <= w->cap) return;
    size_t new_cap = w->cap ? w->cap : 64;
    while (new_cap < w->len + needed) new_cap *= 2;
    unsigned char *new_data = (unsigned char *)realloc(w->data, new_cap);
    if (!new_data) {
        fprintf(stderr, "OOM: tl_writer realloc failed (%zu bytes)\n", new_cap);
        abort();
    }
    w->data = new_data;
    w->cap  = new_cap;
}

static void writer_put(TlWriter *w, const void *src, size_t n) {
    writer_ensure(w, n);
    memcpy(w->data + w->len, src, n);
    w->len += n;
}

/* ---- Writer API ---- */

void tl_writer_init(TlWriter *w) {
    w->data = NULL;
    w->len  = 0;
    w->cap  = 0;
}

void tl_writer_free(TlWriter *w) {
    free(w->data);
    w->data = NULL;
    w->len  = 0;
    w->cap  = 0;
}

void tl_write_raw(TlWriter *w, const unsigned char *data, size_t len) {
    writer_put(w, data, len);
}

static void write_le32(TlWriter *w, uint32_t val) {
    unsigned char buf[4];
    buf[0] = (unsigned char)(val);
    buf[1] = (unsigned char)(val >> 8);
    buf[2] = (unsigned char)(val >> 16);
    buf[3] = (unsigned char)(val >> 24);
    writer_put(w, buf, 4);
}

static void write_le64(TlWriter *w, uint64_t val) {
    unsigned char buf[8];
    for (int i = 0; i < 8; i++)
        buf[i] = (unsigned char)(val >> (8 * i));
    writer_put(w, buf, 8);
}

void tl_write_int32(TlWriter *w, int32_t val) {
    write_le32(w, (uint32_t)val);
}

void tl_write_uint32(TlWriter *w, uint32_t val) {
    write_le32(w, val);
}

void tl_write_int64(TlWriter *w, int64_t val) {
    write_le64(w, (uint64_t)val);
}

void tl_write_uint64(TlWriter *w, uint64_t val) {
    write_le64(w, val);
}

void tl_write_int128(TlWriter *w, const unsigned char val[16]) {
    writer_put(w, val, 16);
}

void tl_write_int256(TlWriter *w, const unsigned char val[32]) {
    writer_put(w, val, 32);
}

void tl_write_double(TlWriter *w, double val) {
    /* IEEE 754 — reinterpret as uint64, write LE */
    uint64_t raw;
    memcpy(&raw, &val, sizeof(raw));
    write_le64(w, raw);
}

void tl_write_bool(TlWriter *w, int val) {
    write_le32(w, val ? TL_BOOL_TRUE : TL_BOOL_FALSE);
}

void tl_write_string(TlWriter *w, const char *s) {
    size_t len = s ? strlen(s) : 0;
    tl_write_bytes(w, (const unsigned char *)s, len);
}

void tl_write_bytes(TlWriter *w, const unsigned char *data, size_t len) {
    /* Length prefix: if first byte < 254, use 1-byte prefix;
       otherwise 0xFE + 3-byte LE length */
    if (len < 254) {
        unsigned char prefix = (unsigned char)len;
        writer_put(w, &prefix, 1);
    } else {
        unsigned char prefix[4];
        prefix[0] = 0xFE;
        prefix[1] = (unsigned char)(len);
        prefix[2] = (unsigned char)(len >> 8);
        prefix[3] = (unsigned char)(len >> 16);
        writer_put(w, prefix, 4);
    }

    if (len > 0) writer_put(w, data, len);

    /* Padding to 4-byte boundary (including the length prefix byte(s)) */
    size_t header = (len < 254) ? 1 : 4;
    size_t total  = header + len;
    size_t pad    = (4 - (total % 4)) % 4;
    if (pad > 0) {
        unsigned char zeros[3] = {0};
        writer_put(w, zeros, pad);
    }
}

void tl_write_vector_begin(TlWriter *w, uint32_t count) {
    write_le32(w, TL_VECTOR_CTOR);
    write_le32(w, count);
}

/* ---- Reader helpers ---- */

static int reader_has(TlReader *r, size_t n) {
    return r->pos + n <= r->len;
}

static uint32_t read_le32(TlReader *r) {
    if (!reader_has(r, 4)) { r->pos = r->len; return 0; }
    uint32_t val = 0;
    val |= (uint32_t)r->data[r->pos];
    val |= (uint32_t)r->data[r->pos + 1] << 8;
    val |= (uint32_t)r->data[r->pos + 2] << 16;
    val |= (uint32_t)r->data[r->pos + 3] << 24;
    r->pos += 4;
    return val;
}

static uint64_t read_le64(TlReader *r) {
    if (!reader_has(r, 8)) { r->pos = r->len; return 0; }
    uint64_t val = 0;
    for (int i = 0; i < 8; i++)
        val |= (uint64_t)r->data[r->pos + i] << (8 * i);
    r->pos += 8;
    return val;
}

/* ---- Reader API ---- */

TlReader tl_reader_init(const unsigned char *data, size_t len) {
    TlReader r = {data, len, 0};
    return r;
}

int tl_reader_ok(const TlReader *r) {
    return r->pos < r->len;
}

int32_t tl_read_int32(TlReader *r) {
    return (int32_t)read_le32(r);
}

uint32_t tl_read_uint32(TlReader *r) {
    return read_le32(r);
}

int64_t tl_read_int64(TlReader *r) {
    return (int64_t)read_le64(r);
}

uint64_t tl_read_uint64(TlReader *r) {
    return read_le64(r);
}

void tl_read_int128(TlReader *r, unsigned char out[16]) {
    if (!reader_has(r, 16)) {
        memset(out, 0, 16);
        r->pos = r->len;
        return;
    }
    memcpy(out, r->data + r->pos, 16);
    r->pos += 16;
}

void tl_read_int256(TlReader *r, unsigned char out[32]) {
    if (!reader_has(r, 32)) {
        memset(out, 0, 32);
        r->pos = r->len;
        return;
    }
    memcpy(out, r->data + r->pos, 32);
    r->pos += 32;
}

double tl_read_double(TlReader *r) {
    uint64_t raw = read_le64(r);
    double val = 0.0;
    memcpy(&val, &raw, sizeof(val));
    return val;
}

int tl_read_bool(TlReader *r) {
    uint32_t magic = read_le32(r);
    if (magic == TL_BOOL_TRUE)  return 1;
    if (magic == TL_BOOL_FALSE) return 0;
    return -1; /* unrecognized */
}

char *tl_read_string(TlReader *r) {
    size_t len = 0;
    unsigned char *bytes = tl_read_bytes(r, &len);
    if (!bytes) return NULL;

    /* Null-terminate */
    char *str = (char *)realloc(bytes, len + 1);
    if (!str) { free(bytes); return NULL; }
    str[len] = '\0';
    return str;
}

unsigned char *tl_read_bytes(TlReader *r, size_t *out_len) {
    *out_len = 0;

    /* Read length prefix */
    if (!reader_has(r, 1)) { r->pos = r->len; return NULL; }

    size_t header_size;
    size_t data_len;

    unsigned char first = r->data[r->pos];
    if (first < 254) {
        data_len    = first;
        header_size = 1;
    } else {
        if (!reader_has(r, 4)) { r->pos = r->len; return NULL; }
        data_len  = (size_t)r->data[r->pos + 1]
                  | ((size_t)r->data[r->pos + 2] << 8)
                  | ((size_t)r->data[r->pos + 3] << 16);
        header_size = 4;
    }

    /* Check we have enough data */
    size_t total_raw = header_size + data_len;
    if (!reader_has(r, total_raw)) { r->pos = r->len; return NULL; }

    /* Allocate and copy data */
    unsigned char *result = (unsigned char *)malloc(data_len);
    if (!result) return NULL;
    if (data_len > 0) memcpy(result, r->data + r->pos + header_size, data_len);

    /* Skip padding */
    size_t padded = (total_raw + 3) & ~(size_t)3;
    r->pos += padded;

    *out_len = data_len;
    return result;
}

void tl_read_raw(TlReader *r, unsigned char *out, size_t len) {
    if (!reader_has(r, len)) {
        memset(out, 0, len);
        r->pos = r->len;
        return;
    }
    memcpy(out, r->data + r->pos, len);
    r->pos += len;
}

void tl_read_skip(TlReader *r, size_t len) {
    if (r->pos + len > r->len) {
        r->pos = r->len;
    } else {
        r->pos += len;
    }
}
