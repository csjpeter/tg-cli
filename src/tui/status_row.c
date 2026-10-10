/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-17 */
/* Status-row rendering implementation. */

/**
 * @file tui/status_row.c
 * @brief Status-row rendering implementation (US-11 v2).
 */

#include "tui/status_row.h"

#include <string.h>

void status_row_init(StatusRow *sr) {
    if (!sr) return;
    memset(sr, 0, sizeof(*sr));
}

void status_row_set_message_kind(StatusRow *sr, const char *msg,
                                 StatusMsgKind kind) {
    if (!sr) return;
    if (!msg) { sr->message[0] = '\0'; sr->kind = STATUS_MSG_PLAIN; return; }
    strncpy(sr->message, msg, STATUS_ROW_MSG_MAX - 1);
    sr->message[STATUS_ROW_MSG_MAX - 1] = '\0';
    sr->kind = kind;
}

void status_row_set_message(StatusRow *sr, const char *msg) {
    status_row_set_message_kind(sr, msg, STATUS_MSG_PLAIN);
}

const char *status_row_hint(StatusMode m) {
    switch (m) {
    case STATUS_MODE_DIALOGS:
        return "[dialogs] j/k move  Enter open  l history  r reload  ? help  Esc quit";
    case STATUS_MODE_HISTORY:
        return "[history] j/k scroll  h dialogs  r reload  ? help  Esc back";
    }
    return "";
}

static tui_style message_style(StatusMsgKind k) {
    switch (k) {
    case STATUS_MSG_PROGRESS: return TUI_STYLE_PROGRESS;
    case STATUS_MSG_OK:       return TUI_STYLE_OK;
    case STATUS_MSG_ERROR:    return TUI_STYLE_ERROR;
    case STATUS_MSG_PLAIN:    break;
    }
    return TUI_STYLE_DEFAULT;
}

void status_row_render(const StatusRow *sr,
                        const tui_rect *pane, tui_screen *screen) {
    if (!sr || !pane || !screen || !pane_is_valid(pane)) return;
    /* The row is always one line; ignore extra pane rows so callers can pass
     * a taller pane without getting extra blanks. */
    tui_canvas c = tui_canvas_sub(tui_canvas_of(screen),
                                  tui_rect_make(pane->row, pane->col,
                                                1, pane->cols));
    tui_canvas_clear(c, TUI_STYLE_DEFAULT);
    if (sr->message[0])
        tui_canvas_put(c, 0, 0, sr->message, message_style(sr->kind));
    else
        tui_canvas_put(c, 0, 0, status_row_hint(sr->mode), TUI_STYLE_HINT);
}
