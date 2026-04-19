/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/read/self.h
 * @brief US-05 — retrieve the authenticated user's own profile.
 *
 * Wraps users.getUsers([inputUserSelf]). Read-only.
 */

#ifndef DOMAIN_READ_SELF_H
#define DOMAIN_READ_SELF_H

#include "api_call.h"
#include "mtproto_session.h"
#include "transport.h"

#include <stdint.h>

/** @brief Own-profile snapshot. Strings are '\0'-terminated, may be empty. */
typedef struct {
    int64_t id;
    char    first_name[128];
    char    last_name[128];
    char    username[64];
    char    phone[32];
    int     is_premium;
    int     is_bot;
} SelfInfo;

/**
 * @brief Fetch the authenticated user's own profile.
 *
 * Issues users.getUsers([inputUserSelf]) via an authenticated session and
 * fills @p out with the parsed response.
 *
 * @param cfg API config (api_id/api_hash already loaded).
 * @param s   Session with a valid auth_key.
 * @param t   Connected transport.
 * @param out Output structure.
 * @return 0 on success, -1 on protocol/parse/RPC error.
 */
int domain_get_self(const ApiConfig *cfg,
                    MtProtoSession *s, Transport *t,
                    SelfInfo *out);

#endif /* DOMAIN_READ_SELF_H */
