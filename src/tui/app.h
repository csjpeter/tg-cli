/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

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

#include "tui/screen.h"
#include "tui/pane.h"
#include "tui/dialog_pane.h"
#include "tui/history_pane.h"
#include "tui/status_row.h"

#include "platform/terminal.h"

typedef enum {
    TUI_FOCUS_DIALOGS = 0,
    TUI_FOCUS_HISTORY = 1,
} TuiFocus;

typedef enum {
    TUI_EVENT_NONE = 0,     /* nothing to do                              */
    TUI_EVENT_REDRAW,       /* state changed; caller should repaint+flip  */
    TUI_EVENT_OPEN_DIALOG,  /* user picked a dialog — load its history    */
    TUI_EVENT_QUIT,         /* user asked to exit                         */
} TuiEvent;

typedef struct {
    Screen      screen;
    Layout      layout;
    DialogPane  dialogs;
    HistoryPane history;
    StatusRow   status;
    TuiFocus    focus;
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

/** Handle a TermKey. Returns a TuiEvent describing the high-level effect.
 *  Navigation and focus switching always return TUI_EVENT_REDRAW; Enter on
 *  the dialogs pane returns TUI_EVENT_OPEN_DIALOG; Ctrl-C returns QUIT. */
TuiEvent tui_app_handle_key(TuiApp *app, TermKey key);

/** Handle a printable character (for `q`, `j/k/h/l`, etc). */
TuiEvent tui_app_handle_char(TuiApp *app, int ch);

/** Stage the current UI state into `app->screen.back`. Does not flip. */
void tui_app_paint(TuiApp *app);

#endif /* TUI_APP_H */
