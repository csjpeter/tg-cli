/**
 * @file tl_serial.h
 * @brief TL (Type Language) binary serialization for MTProto 2.0.
 *
 * Telegram's TL serialization encodes typed values into a compact binary
 * format.  This module provides a cursor-based writer (TlWriter) and reader
 * (TlReader) supporting all types used in the MTProto protocol.
 *
 * Encoding rules:
 *   - int32/uint32  : 4 bytes, little-endian
 *   - int64/uint64  : 8 bytes, little-endian
 *   - int128        : 16 bytes, little-endian
 *   - int256        : 32 bytes, little-endian
 *   - double        : 8 bytes, IEEE 754 little-endian
 *   - bool true     : 0x997275b5 (uint32 magic)
 *   - bool false    : 0xbc799737 (uint32 magic)
 *   - string/bytes  : length prefix (1 or 4 bytes) + data + padding to 4-byte boundary
 *   - vector        : 0x1cb5c415 constructor ID + uint32 count + elements
 */

#ifndef TL_SERIAL_H
#define TL_SERIAL_H

#include <stddef.h>
#include <stdint.h>

/* ---- Writer ---- */

/** Growable byte buffer for building TL-encoded messages. */
typedef struct {
    unsigned char *data; /**< Heap-allocated buffer. */
    size_t         len;  /**< Bytes written so far. */
    size_t         cap;  /**< Allocated capacity. */
} TlWriter;

/**
 * @brief Initialize a writer with zero length.
 * @param w Writer to initialize.
 */
void tl_writer_init(TlWriter *w);

/**
 * @brief Free the internal buffer.
 * @param w Writer to free.
 */
void tl_writer_free(TlWriter *w);

/**
 * @brief Write a raw byte slice (no length prefix, no padding).
 * @param w    Writer.
 * @param data Bytes to append.
 * @param len  Number of bytes.
 */
void tl_write_raw(TlWriter *w, const unsigned char *data, size_t len);

/** Write an int32 (4 bytes LE). */
void tl_write_int32(TlWriter *w, int32_t val);

/** Write a uint32 (4 bytes LE). */
void tl_write_uint32(TlWriter *w, uint32_t val);

/** Write an int64 (8 bytes LE). */
void tl_write_int64(TlWriter *w, int64_t val);

/** Write a uint64 (8 bytes LE). */
void tl_write_uint64(TlWriter *w, uint64_t val);

/** Write an int128 (16 bytes LE). */
void tl_write_int128(TlWriter *w, const unsigned char val[16]);

/** Write an int256 (32 bytes LE). */
void tl_write_int256(TlWriter *w, const unsigned char val[32]);

/** Write a double (8 bytes IEEE 754 LE). */
void tl_write_double(TlWriter *w, double val);

/**
 * @brief Write a TL bool.
 * @param w   Writer.
 * @param val Non-zero for true, zero for false.
 */
void tl_write_bool(TlWriter *w, int val);

/**
 * @brief Write a null-terminated string as TL bytes.
 * Equivalent to tl_write_bytes() with strlen(s).
 * @param w Writer.
 * @param s Null-terminated string.
 */
void tl_write_string(TlWriter *w, const char *s);

/**
 * @brief Write TL bytes: length prefix + data + padding to 4-byte boundary.
 * @param w    Writer.
 * @param data Byte array.
 * @param len  Length of data.
 */
void tl_write_bytes(TlWriter *w, const unsigned char *data, size_t len);

/**
 * @brief Write a TL vector header (constructor ID + count).
 * The caller must then write each element individually.
 * @param w     Writer.
 * @param count Number of elements that will follow.
 */
void tl_write_vector_begin(TlWriter *w, uint32_t count);

/* ---- Reader ---- */

/** Non-owning cursor over a TL-encoded byte buffer. */
typedef struct {
    const unsigned char *data; /**< Buffer pointer (not owned). */
    size_t               len;  /**< Total buffer length. */
    size_t               pos;  /**< Current read position. */
} TlReader;

/**
 * @brief Create a reader over an existing buffer.
 * @param data Buffer start.
 * @param len  Buffer length.
 * @return Initialized reader.
 */
TlReader tl_reader_init(const unsigned char *data, size_t len);

/**
 * @brief Check if the reader has not reached the end.
 * @param r Reader.
 * @return Non-zero if more bytes are available.
 */
int tl_reader_ok(const TlReader *r);

/** Read an int32 (4 bytes LE). Returns 0 on error (past end). */
int32_t tl_read_int32(TlReader *r);

/** Read a uint32 (4 bytes LE). Returns 0 on error. */
uint32_t tl_read_uint32(TlReader *r);

/** Read an int64 (8 bytes LE). Returns 0 on error. */
int64_t tl_read_int64(TlReader *r);

/** Read a uint64 (8 bytes LE). Returns 0 on error. */
uint64_t tl_read_uint64(TlReader *r);

/** Read an int128 (16 bytes LE). Writes zeros on error. */
void tl_read_int128(TlReader *r, unsigned char out[16]);

/** Read an int256 (32 bytes LE). Writes zeros on error. */
void tl_read_int256(TlReader *r, unsigned char out[32]);

/** Read a double (8 bytes IEEE 754 LE). Returns 0.0 on error. */
double tl_read_double(TlReader *r);

/**
 * @brief Read a TL bool.
 * @return 1 for true, 0 for false, -1 for error (unrecognized magic).
 */
int tl_read_bool(TlReader *r);

/**
 * @brief Read a TL string (length-prefixed bytes) as a null-terminated C string.
 * @return Heap-allocated string (caller must free), or NULL on error.
 */
char *tl_read_string(TlReader *r);

/**
 * @brief Read TL bytes (length-prefixed).
 * @param out_len Receives the byte count.
 * @return Heap-allocated byte array (caller must free), or NULL on error.
 */
unsigned char *tl_read_bytes(TlReader *r, size_t *out_len);

/**
 * @brief Read raw bytes into caller-supplied buffer.
 * @param r   Reader.
 * @param out Output buffer.
 * @param len Number of bytes to read.
 */
void tl_read_raw(TlReader *r, unsigned char *out, size_t len);

/**
 * @brief Skip forward in the buffer.
 * @param r   Reader.
 * @param len Number of bytes to skip.
 */
void tl_read_skip(TlReader *r, size_t len);

#endif /* TL_SERIAL_H */
