/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/write/send.h
 * @brief US-P5-03 — messages.sendMessage.
 *
 * First write-capable domain module. Lives under `domain/write/` so it
 * cannot be linked into `tg-cli-ro` by construction (ADR-0005).
 */

#ifndef DOMAIN_WRITE_SEND_H
#define DOMAIN_WRITE_SEND_H

#include "api_call.h"
#include "mtproto_session.h"
#include "mtproto_rpc.h"
#include "transport.h"
#include "domain/read/history.h"   /* HistoryPeer — reused for the input peer. */

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Send a plain text message to @p peer.
 *
 * Builds a messages.sendMessage#d9d75a4 request with a random random_id,
 * then waits for the server's Updates. Only the caller's outgoing
 * message id is extracted for now.
 *
 * @param cfg       API config.
 * @param s         Session with auth_key.
 * @param t         Connected transport.
 * @param peer      Destination (HISTORY_PEER_* kind, access_hash required
 *                  for user / channel).
 * @param message   UTF-8 text to send (1..4096 chars per Telegram limits).
 * @param msg_id_out Optional output; set to the new message id on success
 *                  when the server returned a parsable update.
 * @param err       Optional RPC error output.
 * @return 0 on success, -1 on error.
 */
int domain_send_message(const ApiConfig *cfg,
                         MtProtoSession *s, Transport *t,
                         const HistoryPeer *peer,
                         const char *message,
                         int32_t *msg_id_out,
                         RpcError *err);

/**
 * @brief Like domain_send_message but with an optional reply target.
 *
 * @param reply_to_msg_id  Message id to reply to; 0 = no reply.
 *                         Other parameters mirror domain_send_message.
 */
int domain_send_message_reply(const ApiConfig *cfg,
                               MtProtoSession *s, Transport *t,
                               const HistoryPeer *peer,
                               const char *message,
                               int32_t reply_to_msg_id,
                               int32_t *msg_id_out,
                               RpcError *err);

#endif /* DOMAIN_WRITE_SEND_H */
