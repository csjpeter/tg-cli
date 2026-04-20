/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file core/json_util.c
 * @brief Minimal JSON string escaping — see json_util.h.
 */

#include "json_util.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

size_t json_escape_str(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) return 0;
    if (!src) src = "";

    size_t out = 0;   /* bytes written so far (excl. NUL) */

#define PUSH(c)  do { if (out + 1 < cap) dst[out++] = (char)(c); else out++; } while (0)
#define PUSH2(a,b) do { PUSH(a); PUSH(b); } while (0)

    for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
        unsigned char c = *p;
        switch (c) {
        case '"':  PUSH2('\\', '"');  break;
        case '\\': PUSH2('\\', '\\'); break;
        case '\b': PUSH2('\\', 'b');  break;
        case '\f': PUSH2('\\', 'f');  break;
        case '\n': PUSH2('\\', 'n');  break;
        case '\r': PUSH2('\\', 'r');  break;
        case '\t': PUSH2('\\', 't');  break;
        default:
            if (c < 0x20u) {
                /* Other C0 controls: \uXXXX */
                char seq[7];
                int n = snprintf(seq, sizeof(seq), "\\u%04x", (unsigned)c);
                for (int i = 0; i < n; i++) PUSH(seq[i]);
            } else {
                /* ASCII printable or high byte (UTF-8 pass-through) */
                PUSH(c);
            }
            break;
        }
    }

#undef PUSH2
#undef PUSH

    /* Always NUL-terminate within the buffer. */
    if (out < cap)
        dst[out] = '\0';
    else if (cap > 0)
        dst[cap - 1] = '\0';

    return out;
}
