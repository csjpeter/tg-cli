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
                        const Pane *pane, Screen *screen) {
    if (!sr || !pane || !screen || !pane_is_valid(pane)) return;
    /* The row is always one line; ignore extra pane rows so callers can pass
     * a taller pane without getting extra blanks. */
    pane_fill(pane, screen, 0, 0, pane->cols, SCREEN_ATTR_REVERSE);
    const char *hint = hint_for(sr->mode);
    pane_put_str(pane, screen, 0, 0, hint, SCREEN_ATTR_REVERSE);
    if (sr->message[0]) {
        int mlen = (int)strlen(sr->message);
        int col = pane->cols - mlen - 1;
        if (col < 1) col = 1;
        pane_put_str(pane, screen, 0, col, sr->message, SCREEN_ATTR_REVERSE);
    }
}
