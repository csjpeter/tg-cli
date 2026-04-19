/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file tui/history_pane.c
 * @brief Message history view-model implementation (US-11 v2).
 */

#include "tui/history_pane.h"

#include <stdio.h>
#include <string.h>

void history_pane_init(HistoryPane *hp) {
    if (!hp) return;
    memset(hp, 0, sizeof(*hp));
    list_view_init(&hp->lv);
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
    list_view_set_count(&hp->lv, n);
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
    return snprintf(out, cap, "%c [%d] %s", arrow, e->id, body);
}

void history_pane_render(const HistoryPane *hp,
                          const Pane *pane, Screen *screen) {
    if (!hp || !pane || !screen || !pane_is_valid(pane)) return;
    pane_clear(pane, screen);

    const char *placeholder = NULL;
    if (!hp->peer_loaded)  placeholder = "(select a dialog)";
    else if (hp->count==0) placeholder = "(no messages)";

    if (placeholder) {
        int mid_row = pane->rows / 2;
        int col = (pane->cols - (int)strlen(placeholder)) / 2;
        if (col < 0) col = 0;
        pane_put_str(pane, screen, mid_row, col,
                     placeholder, SCREEN_ATTR_DIM);
        return;
    }

    int first = hp->lv.scroll_top;
    int last  = first + hp->lv.rows_visible;
    if (last > hp->count) last = hp->count;

    for (int i = first; i < last; i++) {
        const HistoryEntry *e = &hp->entries[i];
        char line[HISTORY_TEXT_MAX + 32];
        format_row(e, line, sizeof(line));
        uint8_t attrs = 0;
        if (e->complex) attrs |= SCREEN_ATTR_DIM;
        pane_put_str(pane, screen, i - first, 0, line, attrs);
    }
}
