/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file tui/list_view.c
 * @brief Scroll + selection helper for vertical list panes (US-11 v2).
 */

#include "tui/list_view.h"

#include <string.h>

void list_view_init(ListView *lv) {
    if (!lv) return;
    memset(lv, 0, sizeof(*lv));
    lv->selected = -1;
}

void list_view_reveal_selected(ListView *lv) {
    if (!lv || lv->count <= 0 || lv->rows_visible <= 0) return;
    if (lv->selected < lv->scroll_top) {
        lv->scroll_top = lv->selected;
    } else if (lv->selected >= lv->scroll_top + lv->rows_visible) {
        lv->scroll_top = lv->selected - lv->rows_visible + 1;
    }
    if (lv->scroll_top < 0) lv->scroll_top = 0;
}

void list_view_set_viewport(ListView *lv, int rows_visible) {
    if (!lv) return;
    lv->rows_visible = rows_visible < 0 ? 0 : rows_visible;
    list_view_reveal_selected(lv);
}

void list_view_set_count(ListView *lv, int count) {
    if (!lv) return;
    if (count < 0) count = 0;
    lv->count = count;
    if (count == 0) {
        lv->selected = -1;
        lv->scroll_top = 0;
        return;
    }
    if (lv->selected < 0) lv->selected = 0;
    if (lv->selected >= count) lv->selected = count - 1;
    if (lv->scroll_top > count - 1) lv->scroll_top = count - 1;
    if (lv->scroll_top < 0) lv->scroll_top = 0;
    list_view_reveal_selected(lv);
}

void list_view_move_up(ListView *lv) {
    if (!lv || lv->count == 0) return;
    if (lv->selected > 0) lv->selected--;
    list_view_reveal_selected(lv);
}

void list_view_move_down(ListView *lv) {
    if (!lv || lv->count == 0) return;
    if (lv->selected < lv->count - 1) lv->selected++;
    list_view_reveal_selected(lv);
}

void list_view_page_up(ListView *lv) {
    if (!lv || lv->count == 0) return;
    int step = lv->rows_visible > 0 ? lv->rows_visible : 1;
    lv->selected -= step;
    if (lv->selected < 0) lv->selected = 0;
    list_view_reveal_selected(lv);
}

void list_view_page_down(ListView *lv) {
    if (!lv || lv->count == 0) return;
    int step = lv->rows_visible > 0 ? lv->rows_visible : 1;
    lv->selected += step;
    if (lv->selected >= lv->count) lv->selected = lv->count - 1;
    list_view_reveal_selected(lv);
}

void list_view_home(ListView *lv) {
    if (!lv || lv->count == 0) return;
    lv->selected = 0;
    lv->scroll_top = 0;
}

void list_view_end(ListView *lv) {
    if (!lv || lv->count == 0) return;
    lv->selected = lv->count - 1;
    list_view_reveal_selected(lv);
}

int list_view_is_visible(const ListView *lv, int index) {
    if (!lv || lv->rows_visible <= 0) return 0;
    return index >= lv->scroll_top
        && index <  lv->scroll_top + lv->rows_visible;
}
