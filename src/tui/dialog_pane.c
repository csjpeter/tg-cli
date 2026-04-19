/**
 * @file tui/dialog_pane.c
 * @brief Dialog list view-model implementation (US-11 v2).
 */

#include "tui/dialog_pane.h"

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
    list_view_init(&dp->lv);
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
    list_view_set_count(&dp->lv, n);
}

int dialog_pane_refresh(DialogPane *dp,
                         const ApiConfig *cfg,
                         MtProtoSession *s, Transport *t) {
    if (!dp || !cfg || !s || !t) return -1;
    DialogEntry tmp[DIALOG_PANE_MAX] = {0};
    int count = 0;
    if (domain_get_dialogs(cfg, s, t, DIALOG_PANE_MAX, 0, tmp, &count) != 0)
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
    const char *title = e->title[0] ? e->title : "(no title)";
    if (e->unread_count > 0) {
        return snprintf(out, cap, "%c [%d] %s",
                        prefix, e->unread_count, title);
    }
    return snprintf(out, cap, "%c     %s", prefix, title);
}

void dialog_pane_render(const DialogPane *dp,
                         const Pane *pane, Screen *screen,
                         int focused) {
    if (!dp || !pane || !screen || !pane_is_valid(pane)) return;
    pane_clear(pane, screen);

    if (dp->count == 0) {
        const char *msg = "(no dialogs)";
        int mid_row = pane->rows / 2;
        int col = (pane->cols - (int)strlen(msg)) / 2;
        if (col < 0) col = 0;
        pane_put_str(pane, screen, mid_row, col, msg, SCREEN_ATTR_DIM);
        return;
    }

    int first = dp->lv.scroll_top;
    int last  = first + dp->lv.rows_visible;
    if (last > dp->count) last = dp->count;

    for (int i = first; i < last; i++) {
        const DialogEntry *e = &dp->entries[i];
        char line[192];
        format_row(e, line, sizeof(line));
        uint8_t attrs = 0;
        if (e->unread_count > 0) attrs |= SCREEN_ATTR_BOLD;
        if (focused && i == dp->lv.selected) {
            attrs |= SCREEN_ATTR_REVERSE;
            /* Paint the full row so the highlight extends to the right edge
             * even when the title is short. */
            pane_fill(pane, screen, i - first, 0, pane->cols, attrs);
        }
        pane_put_str(pane, screen, i - first, 0, line, attrs);
    }
}
