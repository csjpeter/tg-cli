/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

#include "pii_redact.h"
#include <string.h>
#include <stdio.h>

void redact_phone(const char *phone, char *out, size_t cap) {
    if (!out || cap == 0) return;
    if (!phone || phone[0] == '\0') {
        snprintf(out, cap, "(null)");
        return;
    }

    /* Skip leading '+' for length accounting */
    const char *digits = phone;
    int has_plus = (phone[0] == '+');
    if (has_plus) digits++;

    size_t dlen = strlen(digits);

    /* If too short to redact meaningfully, mask entirely */
    if (dlen < 5) {
        snprintf(out, cap, "+****");
        return;
    }

    /* Show last 4 digits, mask the rest with '*' */
    size_t keep = 4;
    size_t mask = dlen - keep;

    /* +  <mask stars>  <last 4>  NUL */
    size_t needed = 1 + mask + keep + 1;
    if (needed > cap) {
        /* Not enough room — full mask */
        snprintf(out, cap, "+****");
        return;
    }

    size_t pos = 0;
    out[pos++] = '+';
    for (size_t i = 0; i < mask; i++) out[pos++] = '*';
    /* copy last 4 digits */
    for (size_t i = 0; i < keep; i++) out[pos++] = digits[mask + i];
    out[pos] = '\0';
}
