/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-10-10 */
/* Masked prompt for secrets (2FA password, login code). */

/**
 * @file app/secret_prompt.c
 * @brief See secret_prompt.h.
 */

#include "app/secret_prompt.h"

#include <tuikit/tty.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int secret_prompt_read(const char *label, char *out, size_t cap) {
    if (!out || cap == 0) return -1;
    out[0] = '\0';
    char prompt[96];
    snprintf(prompt, sizeof(prompt), "%s: ", label ? label : "");
    int n = tui_tty_read_password(STDIN_FILENO, STDOUT_FILENO, prompt, out, cap);
    if (n < 0) {
        out[0] = '\0';
        return -1;
    }
    size_t len = strlen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r'))
        out[--len] = '\0';
    return 0;
}
