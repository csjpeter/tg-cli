/**
 * @file domain/write/forward.h
 * @brief US-P5-06 — messages.forwardMessages.
 */

#ifndef DOMAIN_WRITE_FORWARD_H
#define DOMAIN_WRITE_FORWARD_H

#include "api_call.h"
#include "mtproto_session.h"
#include "mtproto_rpc.h"
#include "transport.h"
#include "domain/read/history.h"   /* HistoryPeer */

#include <stdint.h>

/**
 * @brief Forward messages from one peer to another.
 *
 * v1 sets flags=0 — no silent / background / with_my_score tweaks.
 *
 * @param cfg    API config.
 * @param s      Session.
 * @param t      Transport.
 * @param from   Source peer.
 * @param to     Destination peer.
 * @param ids    Message ids on @p from to forward.
 * @param n_ids  Number of ids (1..100).
 * @param err    Optional RPC error.
 * @return 0 on success, -1 on error.
 */
int domain_forward_messages(const ApiConfig *cfg,
                             MtProtoSession *s, Transport *t,
                             const HistoryPeer *from,
                             const HistoryPeer *to,
                             const int32_t *ids, int n_ids,
                             RpcError *err);

#endif /* DOMAIN_WRITE_FORWARD_H */
