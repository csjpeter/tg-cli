/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

#ifndef TUI_HISTORY_PANE_H
#define TUI_HISTORY_PANE_H

/**
 * @file tui/history_pane.h
 * @brief Message history view-model for the TUI (US-11 v2).
 *
 * Shows up to HISTORY_PANE_MAX messages for the currently-selected peer.
 * One line per message: direction marker + id + text (or "(media)" /
 * "(complex)" when the text extractor bailed). Arrow keys scroll via the
 * embedded ListView; no row-level selection highlight (chat history is
 * passive — the user doesn't pick one row at a time).
 */

#include "tui/pane.h"
#include "tui/screen.h"
#include "tui/list_view.h"

#include "domain/read/history.h"
#include "api_call.h"
#include "mtproto_session.h"
#include "transport.h"

#define HISTORY_PANE_MAX 100

typedef struct {
    HistoryEntry entries[HISTORY_PANE_MAX];
    int          count;
    HistoryPeer  peer;
    int          peer_loaded;   /* 1 once set/loaded, 0 before          */
    ListView     lv;
} HistoryPane;

/** Zero out entries, peer, list_view state. */
void history_pane_init(HistoryPane *hp);

/** Replace entries for @p peer with a caller-provided snapshot. Clamps
 *  @p n to HISTORY_PANE_MAX. Sets peer_loaded. */
void history_pane_set_entries(HistoryPane *hp,
                               const HistoryPeer *peer,
                               const HistoryEntry *src, int n);

/** Fetch the latest messages for @p peer and replace the snapshot on
 *  success. Returns 0 on success, -1 on RPC/parse error. */
int  history_pane_load(HistoryPane *hp,
                        const ApiConfig *cfg,
                        MtProtoSession *s, Transport *t,
                        const HistoryPeer *peer);

/** Render the pane onto @p screen within @p pane. When the peer has not
 *  been loaded yet, shows "(select a dialog)". When the peer is loaded
 *  but empty, shows "(no messages)". */
void history_pane_render(const HistoryPane *hp,
                          const Pane *pane, Screen *screen);

#endif /* TUI_HISTORY_PANE_H */
