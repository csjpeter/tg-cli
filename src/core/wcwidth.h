/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file wcwidth.h
 * @brief Portable Unicode column-width helper (mk_wcwidth equivalent).
 *
 * Provides core_wcwidth() for platforms that lack a correct wcwidth(3)
 * (e.g. Windows / MinGW-w64).  POSIX builds call the libc wcwidth(3)
 * directly from platform/posix/terminal.c and do NOT call this function.
 *
 * Unicode version: 15.1 (released September 2023).
 */

#ifndef CORE_WCWIDTH_H
#define CORE_WCWIDTH_H

#include <stdint.h>

/**
 * Returns the number of terminal columns needed to display Unicode
 * codepoint @p cp:
 *   0 — control characters and combining / zero-width marks
 *   1 — normal (Latin, Cyrillic, …) characters
 *   2 — wide (CJK ideographs, fullwidth forms, emoji, …) characters
 *
 * Mirrors the POSIX wcwidth(3) contract but accepts uint32_t to avoid
 * wchar_t size portability issues.
 */
int core_wcwidth(uint32_t cp);

#endif /* CORE_WCWIDTH_H */
