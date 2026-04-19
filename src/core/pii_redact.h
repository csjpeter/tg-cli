/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

#ifndef PII_REDACT_H
#define PII_REDACT_H

#include <stddef.h>

/**
 * @file pii_redact.h
 * @brief Helpers to redact PII (phone numbers, auth codes, hashes) before logging.
 *
 * Phone numbers are masked to show only the last 4 digits: +*****1234.
 * Secrets (auth codes, phone_code_hash) are always replaced with <redacted>.
 */

/**
 * @brief Redacts a phone number, keeping only the last 4 digits visible.
 *
 * Example: "+15551234567" -> "+*******4567"
 * The leading '+' is preserved.  If @p phone is NULL or empty the result
 * is the string "(null)".  If shorter than 5 characters the whole string
 * is replaced with "+****".
 *
 * @param phone  Null-terminated phone string (E.164 or bare digits).
 * @param out    Output buffer.
 * @param cap    Capacity of @p out (including NUL terminator).
 */
void redact_phone(const char *phone, char *out, size_t cap);

#endif /* PII_REDACT_H */
