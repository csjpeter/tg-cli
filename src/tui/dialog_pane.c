/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-17 */
/* Dialog list view-model implementation. */

/**
 * @file tui/dialog_pane.c
 * @brief Dialog list view-model implementation (US-11 v2).
 */

#include "tui/dialog_pane.h"
#include "tui/text_safe.h"

#include <stdio.h>
#include <string.h>

static char kind_prefix(DialogPeerKind k) {
    switch (k) {
    case DIALOG_PEER_USER:    return 'u';
    case DIALOG_PEER_CHAT:    return 't';  /* "team" / group chat */
    case DIALOG_PEER_CHANNEL: return 'c';
    default:                  return '?';
    }
}

void dialog_pane_init(DialogPane *dp) {
    if (!dp) return;
    memset(dp, 0, sizeof(*dp));
    tui_listview_init(&dp->lv);
}

void dialog_pane_set_entries(DialogPane *dp,
                              const DialogEntry *src, int n) {
    if (!dp) return;
    if (n < 0) n = 0;
    if (n > DIALOG_PANE_MAX) n = DIALOG_PANE_MAX;
    if (src && n > 0) {
        memcpy(dp->entries, src, (size_t)n * sizeof(DialogEntry));
    }
    dp->count = n;
    /* Always reset selection to the top after a refresh so the user lands
     * on the most-recent dialog rather than a now-invalid index. */
    dp->lv.selected = (n > 0) ? 0 : -1;
    dp->lv.scroll_top = 0;
    tui_listview_set_count(&dp->lv, n);
}

int dialog_pane_refresh(DialogPane *dp,
                         const ApiConfig *cfg,
                         MtProtoSession *s, Transport *t) {
    if (!dp || !cfg || !s || !t) return -1;
    DialogEntry tmp[DIALOG_PANE_MAX] = {0};
    int count = 0;
    if (domain_get_dialogs(cfg, s, t, DIALOG_PANE_MAX, 0, tmp, &count, NULL) != 0)
        return -1;
    dialog_pane_set_entries(dp, tmp, count);
    return 0;
}

const DialogEntry *dialog_pane_selected(const DialogPane *dp) {
    if (!dp || dp->count == 0) return NULL;
    int idx = dp->lv.selected;
    if (idx < 0 || idx >= dp->count) return NULL;
    return &dp->entries[idx];
}

/* Format one dialog entry as a single line. Returns bytes written
 * (excluding NUL). Produces strings like:
 *     u  Alice                    (plain DM, 0 unread)
 *     c [12] Tech channel         (channel with 12 unread)
 */
static int format_row(const DialogEntry *e, char *out, size_t cap) {
    char prefix = kind_prefix(e->kind);
    char title[sizeof(e->title)];
    tui_text_strip_bidi(e->title[0] ? e->title : "(no title)",
                        title, sizeof(title));
    if (e->unread_count > 0) {
        return snprintf(out, cap, "%c [%d] %s",
                        prefix, e->unread_count, title);
    }
    return snprintf(out, cap, "%c     %s", prefix, title);
}

typedef struct {
    const DialogPane *dp;
    int               focused;
} DialogDraw;

static void draw_dialog_row(void *ctx, tui_canvas row, int index, int selected) {
    const DialogDraw *d = ctx;
    const DialogEntry *e = &d->dp->entries[index];
    char line[192];
    format_row(e, line, sizeof(line));
    uint16_t attrs = 0;
    if (e->unread_count > 0) attrs |= TUI_ATTR_BOLD;
    if (d->focused && selected) {
        attrs |= TUI_ATTR_REVERSE;
        /* Paint the full row so the highlight extends to the right edge
         * even when the title is short. */
        tui_canvas_fill(row, tui_canvas_rect(row), tui_style_attrs(attrs));
    }
    tui_canvas_put(row, 0, 0, line, tui_style_attrs(attrs));
}

void dialog_pane_render(DialogPane *dp,
                         const tui_rect *pane, tui_screen *screen,
                         int focused) {
    if (!dp || !pane || !screen || !pane_is_valid(pane)) return;
    tui_canvas c = tui_canvas_sub(tui_canvas_of(screen), *pane);
    tui_canvas_clear(c, TUI_STYLE_DEFAULT);

    if (dp->count == 0) {
        tui_listview_set_viewport(&dp->lv, pane->rows);
        const char *msg = "(no dialogs)";
        int col = (pane->cols - (int)strlen(msg)) / 2;
        if (col < 0) col = 0;
        tui_canvas_put(c, pane->rows / 2, col, msg,
                       tui_style_attrs(TUI_ATTR_DIM));
        return;
    }

    DialogDraw d = { dp, focused };
    tui_listview_render(&dp->lv, c, TUI_STYLE_DEFAULT, draw_dialog_row, &d);
}
