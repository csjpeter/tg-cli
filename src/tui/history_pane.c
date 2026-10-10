/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-17 */
/* Message history view-model implementation. */

/**
 * @file tui/history_pane.c
 * @brief Message history view-model implementation (US-11 v2).
 */

#include "tui/history_pane.h"
#include "tui/text_safe.h"

#include <stdio.h>
#include <string.h>

void history_pane_init(HistoryPane *hp) {
    if (!hp) return;
    memset(hp, 0, sizeof(*hp));
    tui_listview_init(&hp->lv);
}

void history_pane_set_entries(HistoryPane *hp,
                               const HistoryPeer *peer,
                               const HistoryEntry *src, int n) {
    if (!hp) return;
    if (n < 0) n = 0;
    if (n > HISTORY_PANE_MAX) n = HISTORY_PANE_MAX;
    if (src && n > 0) {
        memcpy(hp->entries, src, (size_t)n * sizeof(HistoryEntry));
    }
    hp->count = n;
    if (peer) hp->peer = *peer;
    hp->peer_loaded = 1;
    hp->lv.selected = (n > 0) ? 0 : -1;
    hp->lv.scroll_top = 0;
    tui_listview_set_count(&hp->lv, n);
}

int history_pane_load(HistoryPane *hp,
                       const ApiConfig *cfg,
                       MtProtoSession *s, Transport *t,
                       const HistoryPeer *peer) {
    if (!hp || !cfg || !s || !t || !peer) return -1;
    HistoryEntry tmp[HISTORY_PANE_MAX] = {0};
    int count = 0;
    if (domain_get_history(cfg, s, t, peer, 0,
                           HISTORY_PANE_MAX, tmp, &count) != 0) {
        return -1;
    }
    history_pane_set_entries(hp, peer, tmp, count);
    return 0;
}

/* Format one history row into a flat line. Returns bytes written
 * (excluding NUL). Examples:
 *     > [42] hello
 *     < [41] (media)
 *     < [40] (complex) */
static int format_row(const HistoryEntry *e, char *out, size_t cap) {
    char arrow = e->out ? '>' : '<';
    const char *body;
    if (e->complex)      body = "(complex)";
    else if (e->text[0]) body = e->text;
    else if (e->media)   body = "(media)";
    else                 body = "";
    char clean[HISTORY_TEXT_MAX];
    tui_text_strip_bidi(body, clean, sizeof(clean));
    return snprintf(out, cap, "%c [%d] %s", arrow, e->id, clean);
}

static void draw_history_row(void *ctx, tui_canvas row, int index,
                             int selected) {
    const HistoryPane *hp = ctx;
    const HistoryEntry *e = &hp->entries[index];
    char line[HISTORY_TEXT_MAX + 32];
    format_row(e, line, sizeof(line));
    uint16_t attrs = e->complex ? TUI_ATTR_DIM : 0;
    (void)selected;
    tui_canvas_put(row, 0, 0, line, tui_style_attrs(attrs));
}

void history_pane_render(HistoryPane *hp,
                          const tui_rect *pane, tui_screen *screen) {
    if (!hp || !pane || !screen || !pane_is_valid(pane)) return;
    tui_canvas c = tui_canvas_sub(tui_canvas_of(screen), *pane);
    tui_canvas_clear(c, TUI_STYLE_DEFAULT);

    const char *placeholder = NULL;
    if (!hp->peer_loaded)  placeholder = "(select a dialog)";
    else if (hp->count==0) placeholder = "(no messages)";

    if (placeholder) {
        tui_listview_set_viewport(&hp->lv, pane->rows);
        int col = (pane->cols - (int)strlen(placeholder)) / 2;
        if (col < 0) col = 0;
        tui_canvas_put(c, pane->rows / 2, col, placeholder,
                       tui_style_attrs(TUI_ATTR_DIM));
        return;
    }

    tui_listview_render(&hp->lv, c, TUI_STYLE_DEFAULT, draw_history_row, hp);
}
