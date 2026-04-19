/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file app/session_store.h
 * @brief Persist and restore MTProto sessions (one per DC) across runs.
 *
 * File layout at `~/.config/tg-cli/session.bin` (mode 0600):
 *   offset 0  — 4 bytes magic "TGCS"
 *   offset 4  — 4 bytes version (currently 2)
 *   offset 8  — 4 bytes home_dc_id (int32 LE)
 *   offset 12 — 4 bytes entry_count (uint32 LE, capped at SESSION_STORE_MAX_DCS)
 *   offset 16 — entry_count * { int32 dc_id, uint64 server_salt,
 *                               uint64 session_id, uint8 auth_key[256] }
 *
 * Per-entry size = 276 bytes. Max payload = 16 + 5*276 = 1396 bytes.
 * Version bump: loader rejects older versions so the operator re-logs in.
 */

#ifndef APP_SESSION_STORE_H
#define APP_SESSION_STORE_H

#include "mtproto_session.h"

#define SESSION_STORE_MAX_DCS 5

/**
 * @brief Persist this session under @p dc_id and mark that DC as home.
 *
 * Upserts the entry in place if @p dc_id already exists; other entries are
 * preserved. The file is written atomically: content is flushed to a sibling
 * session.bin.tmp, fsync'd, then renamed over session.bin.  An exclusive
 * advisory flock is held for the read-modify-write cycle (POSIX only).
 *
 * @return 0 on success, -1 on IO or state error.
 */
int session_store_save(const MtProtoSession *s, int dc_id);

/**
 * @brief Restore the session that was last tagged as home DC.
 *
 * @param s       Initialised MtProtoSession that will be populated.
 * @param dc_id   Receives the home DC id.
 * @return 0 on success, -1 if no file, wrong version, or home DC missing.
 */
int session_store_load(MtProtoSession *s, int *dc_id);

/**
 * @brief Upsert this session under @p dc_id without changing home_dc_id.
 *
 * Used when establishing a secondary-DC session (cross-DC media routing).
 * @return 0 on success, -1 on IO or state error.
 */
int session_store_save_dc(int dc_id, const MtProtoSession *s);

/**
 * @brief Restore the session for a specific DC if one was persisted.
 * @return 0 on success, -1 if the DC has no entry or the file is corrupt.
 */
int session_store_load_dc(int dc_id, MtProtoSession *s);

/** @brief Remove the persisted session file (no-op if absent). */
void session_store_clear(void);

#endif /* APP_SESSION_STORE_H */
