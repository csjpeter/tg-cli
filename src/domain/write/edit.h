/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/write/edit.h
 * @brief US-P5-06 — messages.editMessage (text edit only in v1).
 */

#ifndef DOMAIN_WRITE_EDIT_H
#define DOMAIN_WRITE_EDIT_H

#include "api_call.h"
#include "mtproto_session.h"
#include "mtproto_rpc.h"
#include "transport.h"
#include "domain/read/history.h"   /* HistoryPeer */

#include <stdint.h>

/**
 * @brief Replace the text of a previously-sent message.
 *
 * flags.11 (message) is the only optional we populate in v1.
 *
 * @param cfg       API config.
 * @param s         Session.
 * @param t         Transport.
 * @param peer      Peer that holds the message (self / user / chat / channel).
 * @param msg_id    Target message id.
 * @param new_text  Replacement text (1..4096 UTF-8 bytes).
 * @param err       Optional RPC error output.
 * @return 0 on success, -1 on error.
 */
int domain_edit_message(const ApiConfig *cfg,
                         MtProtoSession *s, Transport *t,
                         const HistoryPeer *peer,
                         int32_t msg_id,
                         const char *new_text,
                         RpcError *err);

#endif /* DOMAIN_WRITE_EDIT_H */
