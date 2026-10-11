/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-17 */
/* Dialog list view-model for the TUI. */

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

#include <tuikit/listview.h>

#include "domain/read/dialogs.h"
#include "api_call.h"
#include "mtproto_session.h"
#include "transport.h"

#define DIALOG_PANE_MAX 100

typedef struct {
    DialogEntry entries[DIALOG_PANE_MAX];
    int         count;
    int         load_failed;   /* 1 when the last refresh could not read */
    tui_listview lv;
} DialogPane;

/** Zero out entries and list_view state. */
void dialog_pane_init(DialogPane *dp);

/** Replace entries with a caller-provided snapshot. Clamps @p n to
 *  DIALOG_PANE_MAX. Resets selection to the first item. */
void dialog_pane_set_entries(DialogPane *dp,
                              const DialogEntry *src, int n);

/** Fetch fresh dialogs from the server and replace entries on success; the
 *  selection and the window stay where they were (clamped to the new
 *  count). On failure the existing snapshot is preserved and
 *  @c load_failed is set. Returns 0 on success, -1 on RPC/parse error. */
int  dialog_pane_refresh(DialogPane *dp,
                          const ApiConfig *cfg,
                          MtProtoSession *s, Transport *t);

/** Render the pane onto @p screen within @p pane. The selected row is
 *  filled in the selection style (bold black on cyan) when @p focused is
 *  non-zero, and underlined when the other pane has the focus, so the
 *  selection never depends on colour alone. An empty list says so and names
 *  the key that fills it; a list that could not be read says that. */
void dialog_pane_render(DialogPane *dp,
                         const tui_rect *pane, tui_screen *screen,
                         int focused);

/** Returns a pointer to the currently-selected entry, or NULL when the
 *  dialog list is empty. */
const DialogEntry *dialog_pane_selected(const DialogPane *dp);

#endif /* TUI_DIALOG_PANE_H */
