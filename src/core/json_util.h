/**
 * @file core/json_util.h
 * @brief Minimal JSON string escaping utilities.
 *
 * Only the escaping primitives needed for NDJSON watch output.  No parser,
 * no full serialiser — keep this thin and dependency-free.
 */

#ifndef CORE_JSON_UTIL_H
#define CORE_JSON_UTIL_H

#include <stddef.h>

/**
 * @brief Write @p src as a JSON-escaped string (without surrounding quotes)
 *        into @p dst.
 *
 * Escapes: @c \\ @c \" and C0 control characters (@c \\uXXXX for 0x00–0x1f,
 * plus @c \\b @c \\f @c \\n @c \\r @c \\t shorthands).  High bytes (0x80+)
 * are passed through verbatim so valid UTF-8 text is preserved.
 *
 * @param dst  Output buffer (NUL-terminated on return).
 * @param cap  Capacity of @p dst in bytes (including NUL).
 * @param src  Input C string; NULL is treated as "".
 * @return Number of bytes written (excluding NUL), like snprintf.
 *         If the return value >= @p cap the output was truncated.
 */
size_t json_escape_str(char *dst, size_t cap, const char *src);

#endif /* CORE_JSON_UTIL_H */
