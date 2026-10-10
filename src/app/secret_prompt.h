/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-10-10 */
/* Masked prompt for secrets (2FA password, login code). */

#ifndef APP_SECRET_PROMPT_H
#define APP_SECRET_PROMPT_H

#include <stddef.h>

/**
 * @file app/secret_prompt.h
 * @brief Reads a secret from the terminal without echo and without history.
 *
 * The prompt is "<label>: ". On a terminal the characters are not echoed and
 * the terminal mode is restored on every return, also on a fatal signal
 * (libtuikit's tui_tty_read_password). When stdin is not a terminal a plain
 * line is read. The text never enters a line history.
 *
 * @param label  Prompt label, e.g. "2FA password".
 * @param out    Receives the NUL-terminated secret, without line ending.
 * @param cap    Capacity of @p out, NUL included.
 * @return 0 on success, -1 on EOF, cancel, error or bad arguments.
 */
int secret_prompt_read(const char *label, char *out, size_t cap);

#endif /* APP_SECRET_PROMPT_H */
