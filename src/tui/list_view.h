#ifndef TUI_LIST_VIEW_H
#define TUI_LIST_VIEW_H

/**
 * @file tui/list_view.h
 * @brief Stateless scroll+selection helper for vertical list panes.
 *
 * ListView owns only the state needed to navigate a flat list of @c count
 * items inside a viewport of @c rows_visible rows:
 *
 *   - @c selected    — currently highlighted row (0 .. count-1, or -1 when
 *                      the list is empty).
 *   - @c scroll_top  — index of the first item rendered at the top of the
 *                      pane (0 .. count - rows_visible).
 *
 * Callers drive it with the navigation functions below (arrow keys, j/k,
 * PgUp/PgDn, Home/End) and then iterate [scroll_top, scroll_top +
 * rows_visible) to paint the pane. The view has no knowledge of what the
 * items actually are — it is a pure geometry helper, so the same primitive
 * backs the dialog list and the history pane.
 */

typedef struct {
    int count;         /* total number of items                          */
    int selected;      /* 0..count-1, or -1 when count == 0              */
    int scroll_top;    /* index of first item visible at the top         */
    int rows_visible;  /* viewport height (in rows)                      */
} ListView;

/** Zero-initialise: empty list, selection = -1, scroll = 0. */
void list_view_init(ListView *lv);

/** Resize the viewport; keeps selection valid and brings it into view. */
void list_view_set_viewport(ListView *lv, int rows_visible);

/** Replace the item count. Clamps selected/scroll_top to the new range;
 *  when the list becomes empty, selected = -1 and scroll_top = 0. */
void list_view_set_count(ListView *lv, int count);

/** Arrow Up / k. No-op when the list is empty or already at top. */
void list_view_move_up(ListView *lv);

/** Arrow Down / j. No-op when the list is empty or already at bottom. */
void list_view_move_down(ListView *lv);

/** PgUp. Moves selection up by one viewport. */
void list_view_page_up(ListView *lv);

/** PgDn. Moves selection down by one viewport. */
void list_view_page_down(ListView *lv);

/** Home. Selects the first item and scrolls to top. */
void list_view_home(ListView *lv);

/** End. Selects the last item and scrolls so it is visible at the bottom. */
void list_view_end(ListView *lv);

/** Adjust @c scroll_top so the current selection is inside the viewport.
 *  Idempotent when selection is already visible. */
void list_view_reveal_selected(ListView *lv);

/** Convenience: returns 1 when @p index is currently visible (inside the
 *  viewport), 0 otherwise. */
int  list_view_is_visible(const ListView *lv, int index);

#endif /* TUI_LIST_VIEW_H */
