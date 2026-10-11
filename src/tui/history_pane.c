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
    int same = hp->peer_loaded && hp->peer.kind == peer->kind
            && hp->peer.peer_id == peer->peer_id;
    if (domain_get_history(cfg, s, t, peer, 0,
                           HISTORY_PANE_MAX, tmp, &count) != 0) {
        if (!same) {
            history_pane_set_entries(hp, peer, NULL, 0);
        }
        hp->load_failed = 1;
        return -1;
    }
    int sel = hp->lv.selected, top = hp->lv.scroll_top;
    history_pane_set_entries(hp, peer, tmp, count);
    hp->load_failed = 0;
    if (same && sel > 0 && hp->count > 0) {
        int rows = hp->lv.rows_visible;
        hp->lv.scroll_top = top < hp->count ? top : 0;
        tui_listview_select(&hp->lv, sel < hp->count ? sel : hp->count - 1);
        tui_listview_set_viewport(&hp->lv, rows);
    }
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

typedef struct {
    const HistoryPane *hp;
    int                focused;
} HistoryDraw;

static void draw_history_row(void *ctx, tui_canvas row, int index,
                             int selected) {
    const HistoryDraw *d = ctx;
    const HistoryPane *hp = d->hp;
    const HistoryEntry *e = &hp->entries[index];
    char line[HISTORY_TEXT_MAX + 32];
    format_row(e, line, sizeof(line));
    tui_style st = tui_style_attrs(e->complex ? TUI_ATTR_DIM : 0);
    if (selected && d->focused) {
        st = TUI_STYLE_SELECTION;
        tui_canvas_fill(row, tui_canvas_rect(row), st);
    } else if (selected) {
        st.attrs |= TUI_ATTR_UNDERLINE;
    }
    tui_canvas_put(row, 0, 0, line, st);
}

void history_pane_render(HistoryPane *hp,
                          const tui_rect *pane, tui_screen *screen,
                          int focused) {
    if (!hp || !pane || !screen || !pane_is_valid(pane)) return;
    tui_canvas c = tui_canvas_sub(tui_canvas_of(screen), *pane);
    tui_canvas_clear(c, TUI_STYLE_DEFAULT);

    const char *placeholder = NULL;
    if (!hp->peer_loaded)  placeholder = "(select a dialog)";
    else if (hp->load_failed && hp->count == 0)
        placeholder = "(could not read messages - r retries)";
    else if (hp->count==0) placeholder = "(no messages - r reloads)";

    if (placeholder) {
        tui_listview_set_viewport(&hp->lv, pane->rows);
        int col = (pane->cols - (int)strlen(placeholder)) / 2;
        if (col < 0) col = 0;
        tui_canvas_put(c, pane->rows / 2, col, placeholder,
                       hp->load_failed ? TUI_STYLE_ERROR
                                       : tui_style_attrs(TUI_ATTR_DIM));
        return;
    }

    HistoryDraw d = { hp, focused };
    tui_listview_render(&hp->lv, c, TUI_STYLE_DEFAULT, draw_history_row, &d);
}
