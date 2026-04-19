/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/write/read_history.h
 * @brief US-P5-04 — mark messages as read.
 *
 * Two TL calls sit behind the same user-facing "read" action because
 * Telegram splits chat-side and channel-side read markers:
 *   - messages.readHistory(peer, max_id) → messages.AffectedMessages
 *     for users, chats and basic groups.
 *   - channels.readHistory(channel, max_id) → Bool
 *     for channels and megagroups.
 *
 * The domain helper dispatches on peer kind and hides the split.
 */

#ifndef DOMAIN_WRITE_READ_HISTORY_H
#define DOMAIN_WRITE_READ_HISTORY_H

#include "api_call.h"
#include "mtproto_session.h"
#include "mtproto_rpc.h"
#include "transport.h"
#include "domain/read/history.h"   /* HistoryPeer */

#include <stdint.h>

/**
 * @brief Mark everything up to @p max_id as read in the given peer.
 *
 * @param cfg      API config.
 * @param s        Session.
 * @param t        Connected transport.
 * @param peer     Peer (user / chat / channel / self).
 * @param max_id   Newest message id to mark as read. 0 = everything.
 * @param err      Optional RPC error output.
 * @return 0 on success, -1 on error.
 */
int domain_mark_read(const ApiConfig *cfg,
                      MtProtoSession *s, Transport *t,
                      const HistoryPeer *peer,
                      int32_t max_id,
                      RpcError *err);

#endif /* DOMAIN_WRITE_READ_HISTORY_H */
