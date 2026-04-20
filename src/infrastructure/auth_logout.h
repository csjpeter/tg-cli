/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file infrastructure/auth_logout.h
 * @brief Server-side logout: auth.logOut#3e72ba19.
 *
 * Sends auth.logOut to invalidate the server-side auth key, then clears the
 * local session file.  If the RPC fails (network error, session already dead)
 * the local file is still cleared so the user's "forget me" intent is always
 * honoured.
 *
 * ADR-0005: this lives in infrastructure/ (not domain/write/) so that
 * tg-cli-ro can call it without linking tg-domain-write.
 */

#ifndef INFRASTRUCTURE_AUTH_LOGOUT_H
#define INFRASTRUCTURE_AUTH_LOGOUT_H

#include "api_call.h"
#include "mtproto_session.h"
#include "transport.h"

#include <stdint.h>

/* ---- TL constructor IDs ---- */
/** auth.logOut#3e72ba19 — request (zero args). */
#define CRC_auth_logOut     0x3e72ba19
/** auth.loggedOut#c3a2835f — success response. */
#define CRC_auth_loggedOut  0xc3a2835f

/**
 * @brief Call auth.logOut on the server and return the raw result.
 *
 * Does NOT clear the session file — the caller controls that.
 * Ignoring a "NOT_AUTHORIZED" error (session already dead) is
 * the caller's responsibility.
 *
 * @param cfg      API config.
 * @param s        MTProto session (must have auth_key).
 * @param t        Connected transport.
 * @return 0 on success (auth.loggedOut received), -1 on error.
 */
int auth_logout_rpc(const ApiConfig *cfg, MtProtoSession *s, Transport *t);

/**
 * @brief Full logout: call auth.logOut on the server, then wipe session.bin.
 *
 * Best-effort: if the RPC fails a warning is logged but the local file is
 * removed regardless.
 *
 * @param cfg      API config (api_id / api_hash).
 * @param s        Initialised, loaded MTProto session.
 * @param t        Connected transport.
 */
void auth_logout(const ApiConfig *cfg, MtProtoSession *s, Transport *t);

/**
 * @brief Register a callback invoked by auth_logout() after the local
 *        session has been cleared.
 *
 * Intended for binaries to drop in-process caches that are scoped to the
 * logged-out account (e.g. the resolver cache in user_info.c).  Passing
 * @c NULL disables the callback.  A single slot is supported — a later
 * call replaces the previously registered callback.  The layering rule is
 * that infrastructure/ cannot reach domain/ directly, so the binary's
 * main() registers the flush function at startup.
 */
void auth_logout_set_cache_flush_cb(void (*cb)(void));

#endif /* INFRASTRUCTURE_AUTH_LOGOUT_H */
