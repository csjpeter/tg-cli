/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-10-10 */
/* Display-text filter for Telegram-supplied strings shown in the TUI. */

#ifndef TUI_TEXT_SAFE_H
#define TUI_TEXT_SAFE_H

#include <stddef.h>

/**
 * @file tui/text_safe.h
 * @brief Removes bidirectional control characters from text that came from
 *        the network before it is drawn.
 *
 * libtuikit's tui_screen_put() already neutralises ESC, BEL, C1 controls and
 * DEL.  It passes the bidi override and isolate controls through (they have
 * no width), and a terminal that implements bidi would let a message
 * re-order the text around it ("Trojan source" style spoofing).  The
 * characters removed are U+061C, U+200E, U+200F, U+202A..U+202E and
 * U+2066..U+2069.
 */

/** Copy @p in to @p out (capacity @p cap, NUL included) without the
 *  characters listed above. Always NUL-terminates when cap > 0; truncates
 *  on a character boundary. Returns the length written. */
size_t tui_text_strip_bidi(const char *in, char *out, size_t cap);

#endif /* TUI_TEXT_SAFE_H */
