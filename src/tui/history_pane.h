/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-17 */
/* Message history view-model for the TUI. */

#ifndef TUI_HISTORY_PANE_H
#define TUI_HISTORY_PANE_H

/**
 * @file tui/history_pane.h
 * @brief Message history view-model for the TUI (US-11 v2).
 *
 * Shows up to HISTORY_PANE_MAX messages for the currently-selected peer.
 * One line per message: direction marker + id + text (or "(media)" /
 * "(complex)" when the text extractor bailed). Arrow keys scroll via the
 * embedded ListView; the row under the cursor is shown in the selection
 * style while the pane has the focus.
 */

#include "tui/pane.h"

#include <tuikit/listview.h>

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
    int          load_failed;   /* 1 when the last load could not read  */
    tui_listview lv;
} HistoryPane;

/** Zero out entries, peer, list_view state. */
void history_pane_init(HistoryPane *hp);

/** Replace entries for @p peer with a caller-provided snapshot. Clamps
 *  @p n to HISTORY_PANE_MAX. Sets peer_loaded. */
void history_pane_set_entries(HistoryPane *hp,
                               const HistoryPeer *peer,
                               const HistoryEntry *src, int n);

/** Fetch the latest messages for @p peer and replace the snapshot on
 *  success. Reloading the peer that is already shown keeps the cursor and
 *  the window; a different peer starts on its first row. On failure
 *  @c load_failed is set (a different peer's old messages are dropped).
 *  Returns 0 on success, -1 on RPC/parse error. */
int  history_pane_load(HistoryPane *hp,
                        const ApiConfig *cfg,
                        MtProtoSession *s, Transport *t,
                        const HistoryPeer *peer);

/** Render the pane onto @p screen within @p pane. When the peer has not
 *  been loaded yet, shows "(select a dialog)". When the peer is loaded
 *  but empty, shows "(no messages - r reloads)"; when it could not be read,
 *  says so in the error style. @p focused picks the selection style for the
 *  row under the cursor (underline when not focused). */
void history_pane_render(HistoryPane *hp,
                          const tui_rect *pane, tui_screen *screen,
                          int focused);

#endif /* TUI_HISTORY_PANE_H */
