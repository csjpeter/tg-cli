/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-17 */
/* TUI state machine. */

#ifndef TUI_APP_H
#define TUI_APP_H

/**
 * @file tui/app.h
 * @brief TUI state machine for US-11 v2.
 *
 * `TuiApp` wires the three panes (dialogs / history / status) and the
 * screen together. It is a pure state machine: `tui_app_handle_key` and
 * `tui_app_handle_char` take one input event and return a high-level
 * TuiEvent describing what the caller should do next (redraw, load
 * history for the newly-selected dialog, quit…). No terminal IO is
 * performed — that lives in the event-loop glue (tg_tui.c).
 *
 * Rendering is split from terminal IO too: `tui_app_paint` stages the
 * current state into the back buffer; the caller calls `screen_flip`
 * when it wants the terminal to catch up. This keeps the state machine
 * testable in isolation.
 */

#include "tui/pane.h"
#include "tui/dialog_pane.h"
#include "tui/history_pane.h"
#include "tui/status_row.h"

#include <tuikit/dialog.h>
#include <tuikit/event.h>
#include <tuikit/screen.h>

typedef enum {
    TUI_FOCUS_DIALOGS = 0,
    TUI_FOCUS_HISTORY = 1,
} TuiFocus;

typedef enum {
    TUI_EVENT_NONE = 0,     /* nothing to do                              */
    TUI_EVENT_REDRAW,       /* state changed; caller should repaint+flip  */
    TUI_EVENT_OPEN_DIALOG,  /* user picked a dialog — load its history    */
    TUI_EVENT_RELOAD,       /* r: reload dialogs (and the open history)   */
    TUI_EVENT_QUIT,         /* user asked to exit                         */
} TuiEvent;

typedef struct {
    tui_screen *screen;
    Layout      layout;
    DialogPane  dialogs;
    HistoryPane history;
    StatusRow   status;
    TuiFocus    focus;
    tui_dialog  help;       /* the ? overlay; valid while help_open        */
    int         help_open;
    int         rows;
    int         cols;
} TuiApp;

/** Initialize screen + panes for the given terminal size. Returns 0 on
 *  success, -1 on allocation failure or too-small dimensions. */
int  tui_app_init(TuiApp *app, int rows, int cols);

/** Release the screen. Caller owns any domain data (cfg, session). */
void tui_app_free(TuiApp *app);

/** React to SIGWINCH-style resize: frees + reinits the screen at the new
 *  size and recomputes the layout. Returns 0 on success, -1 on failure. */
int  tui_app_resize(TuiApp *app, int rows, int cols);

/** Feed one decoded key event (TUI_EV_KEY), per
 *  policies/tui-interface-model.md. Ctrl-C quits at once. Esc, q and
 *  Backspace go back (history -> dialogs); Esc on the top level (the dialog
 *  list) quits, q and Backspace there change nothing. ? opens the help
 *  overlay (a bordered, titled box; Esc, q, Backspace close it, the movement
 *  keys scroll it, any other key changes nothing). r reloads. Left/Right and
 *  h/l move focus between the panes; Up/Down, PgUp/PgDn, Home/End, j/k, g/G,
 *  Ctrl-F/Ctrl-B move in the focused list; Enter opens the selected dialog.
 *  F1..F12 are unbound. A keypress clears the status message. Other events
 *  (and NULL) yield TUI_EVENT_NONE. */
TuiEvent tui_app_handle_key(TuiApp *app, const tui_event *ev);

/** Stage the current UI state into `app->screen.back`. Does not flip. */
void tui_app_paint(TuiApp *app);

#endif /* TUI_APP_H */
