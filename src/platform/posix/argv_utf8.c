/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file platform/posix/argv_utf8.c
 * @brief No-op argv normalisation for POSIX platforms.
 *
 * On Linux / macOS / Android the shell passes argv already encoded in the
 * locale's character set (almost always UTF-8).  No conversion is needed.
 */

#include "../path.h"

void platform_normalize_argv(int *argc, char ***argv)
{
    /* POSIX: argv is already UTF-8 — nothing to do. */
    (void)argc;
    (void)argv;
}
