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

void status_row_set_message(StatusRow *sr, const char *msg) {
    if (!sr) return;
    if (!msg) { sr->message[0] = '\0'; return; }
    strncpy(sr->message, msg, STATUS_ROW_MSG_MAX - 1);
    sr->message[STATUS_ROW_MSG_MAX - 1] = '\0';
}

static const char *hint_for(StatusMode m) {
    switch (m) {
    case STATUS_MODE_DIALOGS:
        return "[dialogs] j/k move  Enter open  Tab history  q quit";
    case STATUS_MODE_HISTORY:
        return "[history] j/k scroll  Tab dialogs  q quit";
    }
    return "";
}

void status_row_render(const StatusRow *sr,
                        const tui_rect *pane, tui_screen *screen) {
    if (!sr || !pane || !screen || !pane_is_valid(pane)) return;
    /* The row is always one line; ignore extra pane rows so callers can pass
     * a taller pane without getting extra blanks. */
    tui_style rev = tui_style_attrs(TUI_ATTR_REVERSE);
    tui_canvas c = tui_canvas_sub(tui_canvas_of(screen),
                                  tui_rect_make(pane->row, pane->col,
                                                1, pane->cols));
    tui_canvas_fill(c, tui_canvas_rect(c), rev);
    tui_canvas_put(c, 0, 0, hint_for(sr->mode), rev);
    if (sr->message[0]) {
        int mlen = (int)strlen(sr->message);
        int col = pane->cols - mlen - 1;
        if (col < 1) col = 1;
        tui_canvas_put(c, 0, col, sr->message, rev);
    }
}
