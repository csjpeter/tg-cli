/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file domain/read/updates.h
 * @brief US-07 — updates.getState / updates.getDifference.
 *
 * V1 captures the high-water marks (pts/qts/date/seq) and the counts of
 * new_messages / other_updates per poll. Message body extraction lives
 * in v2 (shares parsing with US-06).
 */

#ifndef DOMAIN_READ_UPDATES_H
#define DOMAIN_READ_UPDATES_H

#include "api_call.h"
#include "mtproto_session.h"
#include "transport.h"
#include "domain/read/history.h"   /* HistoryEntry */

#include <stdint.h>

typedef struct {
    int32_t pts;
    int32_t qts;
    int32_t date;
    int32_t seq;
    int32_t unread_count;
} UpdatesState;

/** @brief Max new messages returned in one UpdatesDifference. */
#define UPDATES_MAX_MESSAGES 32

typedef struct {
    int32_t       new_messages_count;     /**< Number actually written.    */
    int32_t       other_updates_count;
    HistoryEntry  new_messages[UPDATES_MAX_MESSAGES];
    UpdatesState  next_state;              /**< Updated high-water marks.   */
    int           is_too_long;             /**< Server signalled Too Long.  */
    int           is_empty;                /**< differenceEmpty.            */
} UpdatesDifference;

/** @brief Fetch updates.getState to seed the poll loop. */
int domain_updates_state(const ApiConfig *cfg,
                          MtProtoSession *s, Transport *t,
                          UpdatesState *out);

/** @brief Fetch updates.getDifference given the last seen state. */
int domain_updates_difference(const ApiConfig *cfg,
                               MtProtoSession *s, Transport *t,
                               const UpdatesState *in,
                               UpdatesDifference *out);

#endif /* DOMAIN_READ_UPDATES_H */
