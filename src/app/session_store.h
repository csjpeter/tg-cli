/**
 * @file app/session_store.h
 * @brief Persist and restore the MTProto session across runs.
 *
 * File layout at `~/.config/tg-cli/session.bin` (mode 0600):
 *   offset 0  — 4 bytes magic "TGCS"
 *   offset 4  — 4 bytes version (currently 1)
 *   offset 8  — 4 bytes dc_id (int32 LE)
 *   offset 12 — 8 bytes server_salt (uint64 LE)
 *   offset 20 — 8 bytes session_id (uint64 LE)
 *   offset 28 — 256 bytes auth_key
 *
 * Total = 284 bytes. Simple TLV-free layout; additional fields trigger a
 * version bump that causes restore to fail safely (operator re-login).
 */

#ifndef APP_SESSION_STORE_H
#define APP_SESSION_STORE_H

#include "mtproto_session.h"

/**
 * @brief Save the session (auth_key + salt + session_id) plus the DC id.
 * @return 0 on success, -1 on error.
 */
int session_store_save(const MtProtoSession *s, int dc_id);

/**
 * @brief Restore a previously saved session.
 * @param s       Initialised MtProtoSession that will be populated.
 * @param dc_id   Receives the DC id.
 * @return 0 on success, -1 if no file or corrupt / wrong version.
 */
int session_store_load(MtProtoSession *s, int *dc_id);

/** @brief Remove the persisted session file (no-op if absent). */
void session_store_clear(void);

#endif /* APP_SESSION_STORE_H */
