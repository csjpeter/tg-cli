/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

#ifndef TUI_DIALOG_PANE_H
#define TUI_DIALOG_PANE_H

/**
 * @file tui/dialog_pane.h
 * @brief Dialog list view-model for the TUI (US-11 v2).
 *
 * Owns a snapshot of DialogEntry rows (fetched from messages.getDialogs) and
 * a ListView that tracks scroll + selection. The pane is rendered onto a Pane
 * rectangle on a Screen; key navigation is handled by calling the list_view_*
 * functions on @c dp->lv directly from the event loop.
 */

#include "tui/pane.h"
#include "tui/screen.h"
#include "tui/list_view.h"

#include "domain/read/dialogs.h"
#include "api_call.h"
#include "mtproto_session.h"
#include "transport.h"

#define DIALOG_PANE_MAX 100

typedef struct {
    DialogEntry entries[DIALOG_PANE_MAX];
    int         count;
    ListView    lv;
} DialogPane;

/** Zero out entries and list_view state. */
void dialog_pane_init(DialogPane *dp);

/** Replace entries with a caller-provided snapshot. Clamps @p n to
 *  DIALOG_PANE_MAX. Resets selection to the first item. */
void dialog_pane_set_entries(DialogPane *dp,
                              const DialogEntry *src, int n);

/** Fetch fresh dialogs from the server and replace entries on success.
 *  On failure the existing snapshot is preserved. Returns 0 on success,
 *  -1 on RPC/parse error. */
int  dialog_pane_refresh(DialogPane *dp,
                          const ApiConfig *cfg,
                          MtProtoSession *s, Transport *t);

/** Render the pane onto @p screen within @p pane. When @p focused is
 *  non-zero the selected row is rendered with reverse video so users can
 *  tell which side has input focus. */
void dialog_pane_render(const DialogPane *dp,
                         const Pane *pane, Screen *screen,
                         int focused);

/** Returns a pointer to the currently-selected entry, or NULL when the
 *  dialog list is empty. */
const DialogEntry *dialog_pane_selected(const DialogPane *dp);

#endif /* TUI_DIALOG_PANE_H */
