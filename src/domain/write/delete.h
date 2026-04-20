/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/write/delete.h
 * @brief US-P5-06 — messages.deleteMessages + channels.deleteMessages.
 */

#ifndef DOMAIN_WRITE_DELETE_H
#define DOMAIN_WRITE_DELETE_H

#include "api_call.h"
#include "mtproto_session.h"
#include "mtproto_rpc.h"
#include "transport.h"
#include "domain/read/history.h"   /* HistoryPeer */

#include <stdint.h>

/**
 * @brief Delete one or more messages.
 *
 * @param cfg    API config.
 * @param s      Session.
 * @param t      Transport.
 * @param peer   Peer — only its kind + (for channels) id+access_hash matter.
 *               The server routes deletions by the (peer, ids) pair for
 *               channels; for users/chats the message ids are global.
 * @param ids    Array of message ids to delete.
 * @param n_ids  Number of ids (1..100).
 * @param revoke Non-zero to also remove from the recipient's side.
 * @param err    Optional RPC error.
 * @return 0 on success, -1 on error.
 */
int domain_delete_messages(const ApiConfig *cfg,
                            MtProtoSession *s, Transport *t,
                            const HistoryPeer *peer,
                            const int32_t *ids, int n_ids,
                            int revoke,
                            RpcError *err);

#endif /* DOMAIN_WRITE_DELETE_H */
