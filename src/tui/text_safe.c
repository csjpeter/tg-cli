/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-10-10 */
/* Display-text filter for Telegram-supplied strings shown in the TUI. */

/**
 * @file tui/text_safe.c
 * @brief Bidi-control removal (see text_safe.h).
 */

#include "tui/text_safe.h"

#include <string.h>

/* Length of the bidi control at @p s, or 0 when @p s does not start with one.
 * U+061C is D8 9C; U+200E/F are E2 80 8E/8F; U+202A..202E are E2 80 AA..AE;
 * U+2066..2069 are E2 81 A6..A9. */
static size_t bidi_len(const unsigned char *s) {
    if (s[0] == 0xD8 && s[1] == 0x9C) return 2;
    if (s[0] == 0xE2 && s[1] == 0x80
        && (s[2] == 0x8E || s[2] == 0x8F || (s[2] >= 0xAA && s[2] <= 0xAE)))
        return 3;
    if (s[0] == 0xE2 && s[1] == 0x81 && s[2] >= 0xA6 && s[2] <= 0xA9)
        return 3;
    return 0;
}

/* Byte length of the UTF-8 character at @p s (1 for a malformed byte). */
static size_t char_len(const unsigned char *s) {
    size_t n = s[0] < 0x80 ? 1 : s[0] >= 0xF0 ? 4 : s[0] >= 0xE0 ? 3
             : s[0] >= 0xC2 ? 2 : 1;
    for (size_t i = 1; i < n; i++)
        if ((s[i] & 0xC0) != 0x80) return 1;
    return n;
}

size_t tui_text_strip_bidi(const char *in, char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!in) return 0;
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)in;
    while (*p) {
        size_t skip = bidi_len(p);
        if (skip) { p += skip; continue; }
        size_t n = char_len(p);
        if (o + n >= cap) break;
        memcpy(out + o, p, n);
        o += n;
        p += n;
    }
    out[o] = '\0';
    return o;
}
