/**
 * @file tests/unit/test_tui_dialog_pane.c
 * @brief Unit tests for the dialog pane view-model (US-11 v2).
 */

#include "test_helpers.h"
#include "tui/dialog_pane.h"

#include <string.h>

/* --- Helpers --- */

static DialogEntry mk_entry(DialogPeerKind kind, int64_t id,
                             const char *title, int unread) {
    DialogEntry e = {0};
    e.kind = kind;
    e.peer_id = id;
    e.unread_count = unread;
    if (title) {
        strncpy(e.title, title, sizeof(e.title) - 1);
        e.title[sizeof(e.title) - 1] = '\0';
    }
    return e;
}

/* Scan the given row of the back buffer for the first run of non-blank
 * cells and copy the codepoints out as an ASCII string (good enough for
 * tests that use ASCII titles). */
static void row_text(const Screen *s, int row, char *out, size_t cap) {
    size_t oi = 0;
    for (int c = 0; c < s->cols && oi + 1 < cap; c++) {
        ScreenCell cell = s->back[row * s->cols + c];
        out[oi++] = (cell.cp && cell.cp < 128) ? (char)cell.cp : ' ';
    }
    /* Trim trailing spaces. */
    while (oi > 0 && out[oi - 1] == ' ') oi--;
    out[oi] = '\0';
}

/* --- State tests --- */

static void test_init_is_empty(void) {
    DialogPane dp;
    dialog_pane_init(&dp);
    ASSERT(dp.count == 0, "count 0");
    ASSERT(dp.lv.selected == -1, "selected -1");
    ASSERT(dialog_pane_selected(&dp) == NULL, "no selection returns NULL");
}

static void test_set_entries_populates_and_resets_selection(void) {
    DialogPane dp; dialog_pane_init(&dp);
    DialogEntry src[3] = {
        mk_entry(DIALOG_PEER_USER, 1, "Alice", 0),
        mk_entry(DIALOG_PEER_CHANNEL, 2, "News", 5),
        mk_entry(DIALOG_PEER_CHAT, 3, "Team", 0),
    };
    dialog_pane_set_entries(&dp, src, 3);
    ASSERT(dp.count == 3, "3 entries");
    ASSERT(dp.lv.selected == 0, "selected first");
    const DialogEntry *sel = dialog_pane_selected(&dp);
    ASSERT(sel != NULL && sel->peer_id == 1, "selected is Alice");
}

static void test_set_entries_clamps_overflow(void) {
    DialogPane dp; dialog_pane_init(&dp);
    DialogEntry src[10] = {0};
    for (int i = 0; i < 10; i++)
        src[i] = mk_entry(DIALOG_PEER_USER, i + 1, "x", 0);
    /* Ask to copy 500 — should clamp to DIALOG_PANE_MAX (100) or src length. */
    dialog_pane_set_entries(&dp, src, 500);
    ASSERT(dp.count == DIALOG_PANE_MAX, "clamped to max");
    /* First 10 entries have real data; remaining are garbage we don't read. */
    ASSERT(dp.entries[0].peer_id == 1, "first entry copied");
    /* Verify set_entries with 0 resets to empty. */
    dialog_pane_set_entries(&dp, NULL, 0);
    ASSERT(dp.count == 0 && dp.lv.selected == -1, "reset to empty");
}

static void test_selected_after_navigation(void) {
    DialogPane dp; dialog_pane_init(&dp);
    DialogEntry src[4];
    for (int i = 0; i < 4; i++)
        src[i] = mk_entry(DIALOG_PEER_USER, 100 + i, "u", 0);
    dialog_pane_set_entries(&dp, src, 4);
    list_view_move_down(&dp.lv);
    list_view_move_down(&dp.lv);
    const DialogEntry *sel = dialog_pane_selected(&dp);
    ASSERT(sel != NULL && sel->peer_id == 102, "selection follows list_view");
}

/* --- Render tests --- */

static void test_render_empty_pane_shows_placeholder(void) {
    Screen s; ASSERT(screen_init(&s, 5, 20) == 0, "init screen");
    DialogPane dp; dialog_pane_init(&dp);
    Pane p = { .row = 0, .col = 0, .rows = 5, .cols = 20 };
    dp.lv.rows_visible = p.rows;
    dialog_pane_render(&dp, &p, &s, /*focused*/ 1);

    /* Row 2 (middle of 5 rows) should hold the placeholder text. */
    char buf[32]; row_text(&s, 2, buf, sizeof(buf));
    ASSERT(strstr(buf, "(no dialogs)") != NULL, "placeholder rendered");
    screen_free(&s);
}

static void test_render_writes_kind_prefix_and_title(void) {
    Screen s; ASSERT(screen_init(&s, 5, 30) == 0, "init screen");
    DialogPane dp; dialog_pane_init(&dp);
    DialogEntry src[3] = {
        mk_entry(DIALOG_PEER_USER, 1, "Alice", 0),
        mk_entry(DIALOG_PEER_CHANNEL, 2, "News channel", 5),
        mk_entry(DIALOG_PEER_CHAT, 3, "Team", 0),
    };
    dialog_pane_set_entries(&dp, src, 3);
    Pane p = { .row = 0, .col = 0, .rows = 5, .cols = 30 };
    dp.lv.rows_visible = p.rows;
    dialog_pane_render(&dp, &p, &s, /*focused*/ 0);

    char row0[64]; row_text(&s, 0, row0, sizeof(row0));
    ASSERT(row0[0] == 'u', "row 0 begins with 'u' (user)");
    ASSERT(strstr(row0, "Alice") != NULL, "row 0 contains title");

    char row1[64]; row_text(&s, 1, row1, sizeof(row1));
    ASSERT(row1[0] == 'c', "row 1 begins with 'c' (channel)");
    ASSERT(strstr(row1, "[5]") != NULL, "row 1 shows unread badge");
    ASSERT(strstr(row1, "News channel") != NULL, "row 1 contains title");

    char row2[64]; row_text(&s, 2, row2, sizeof(row2));
    ASSERT(row2[0] == 't', "row 2 begins with 't' (chat)");
    ASSERT(strstr(row2, "Team") != NULL, "row 2 contains title");
    screen_free(&s);
}

static void test_render_highlights_selection_when_focused(void) {
    Screen s; ASSERT(screen_init(&s, 3, 25) == 0, "init screen");
    DialogPane dp; dialog_pane_init(&dp);
    DialogEntry src[3] = {
        mk_entry(DIALOG_PEER_USER, 1, "A", 0),
        mk_entry(DIALOG_PEER_USER, 2, "B", 0),
        mk_entry(DIALOG_PEER_USER, 3, "C", 0),
    };
    dialog_pane_set_entries(&dp, src, 3);
    list_view_move_down(&dp.lv); /* select "B" on row 1 */
    Pane p = { .row = 0, .col = 0, .rows = 3, .cols = 25 };
    dp.lv.rows_visible = p.rows;
    dialog_pane_render(&dp, &p, &s, /*focused*/ 1);

    /* Row 0 (not selected) should not be reversed. */
    ASSERT((s.back[0 * 25 + 0].attrs & SCREEN_ATTR_REVERSE) == 0,
           "row 0 not highlighted");
    /* Row 1 (selected) should be reversed across the full pane width. */
    for (int c = 0; c < p.cols; c++) {
        uint8_t a = s.back[1 * 25 + c].attrs;
        ASSERT(a & SCREEN_ATTR_REVERSE, "row 1 fully highlighted");
    }
    /* Row 2 should not be reversed. */
    ASSERT((s.back[2 * 25 + 0].attrs & SCREEN_ATTR_REVERSE) == 0,
           "row 2 not highlighted");
    screen_free(&s);
}

static void test_render_does_not_highlight_when_unfocused(void) {
    Screen s; ASSERT(screen_init(&s, 2, 20) == 0, "init screen");
    DialogPane dp; dialog_pane_init(&dp);
    DialogEntry src[2] = {
        mk_entry(DIALOG_PEER_USER, 1, "A", 0),
        mk_entry(DIALOG_PEER_USER, 2, "B", 0),
    };
    dialog_pane_set_entries(&dp, src, 2);
    Pane p = { .row = 0, .col = 0, .rows = 2, .cols = 20 };
    dp.lv.rows_visible = p.rows;
    dialog_pane_render(&dp, &p, &s, /*focused*/ 0);
    /* Selected row stays on row 0; must NOT be reversed. */
    for (int c = 0; c < p.cols; c++) {
        ASSERT((s.back[c].attrs & SCREEN_ATTR_REVERSE) == 0,
               "no reverse when unfocused");
    }
    screen_free(&s);
}

static void test_render_bolds_unread_rows(void) {
    Screen s; ASSERT(screen_init(&s, 2, 25) == 0, "init screen");
    DialogPane dp; dialog_pane_init(&dp);
    DialogEntry src[2] = {
        mk_entry(DIALOG_PEER_USER, 1, "ReadRow", 0),
        mk_entry(DIALOG_PEER_USER, 2, "HasUnread", 3),
    };
    dialog_pane_set_entries(&dp, src, 2);
    Pane p = { .row = 0, .col = 0, .rows = 2, .cols = 25 };
    dp.lv.rows_visible = p.rows;
    dialog_pane_render(&dp, &p, &s, /*focused*/ 0);
    ASSERT((s.back[0].attrs & SCREEN_ATTR_BOLD) == 0, "no unread → not bold");
    ASSERT(s.back[25].attrs & SCREEN_ATTR_BOLD, "unread row is bold");
    screen_free(&s);
}

static void test_render_respects_scroll_top(void) {
    Screen s; ASSERT(screen_init(&s, 3, 20) == 0, "init screen");
    DialogPane dp; dialog_pane_init(&dp);
    DialogEntry src[6];
    for (int i = 0; i < 6; i++)
        src[i] = mk_entry(DIALOG_PEER_USER, i + 1,
                          (char[]){ (char)('A' + i), '\0' }, 0);
    dialog_pane_set_entries(&dp, src, 6);
    Pane p = { .row = 0, .col = 0, .rows = 3, .cols = 20 };
    dp.lv.rows_visible = p.rows;
    /* Scroll down so rows 3,4,5 are visible. */
    list_view_end(&dp.lv);
    dialog_pane_render(&dp, &p, &s, /*focused*/ 1);
    char row0[32]; row_text(&s, 0, row0, sizeof(row0));
    char row2[32]; row_text(&s, 2, row2, sizeof(row2));
    ASSERT(strstr(row0, "D") != NULL, "row 0 shows D (index 3)");
    ASSERT(strstr(row2, "F") != NULL, "row 2 shows F (index 5)");
    screen_free(&s);
}

static void test_render_null_args_are_noops(void) {
    Screen s; ASSERT(screen_init(&s, 2, 10) == 0, "init screen");
    Pane p = { .row = 0, .col = 0, .rows = 2, .cols = 10 };
    DialogPane dp; dialog_pane_init(&dp);
    dialog_pane_render(NULL, &p, &s, 0);
    dialog_pane_render(&dp, NULL, &s, 0);
    dialog_pane_render(&dp, &p, NULL, 0);
    /* No crash; back grid untouched. */
    ASSERT(s.back[0].cp == ' ', "back untouched");
    screen_free(&s);
}

void test_tui_dialog_pane_run(void) {
    RUN_TEST(test_init_is_empty);
    RUN_TEST(test_set_entries_populates_and_resets_selection);
    RUN_TEST(test_set_entries_clamps_overflow);
    RUN_TEST(test_selected_after_navigation);
    RUN_TEST(test_render_empty_pane_shows_placeholder);
    RUN_TEST(test_render_writes_kind_prefix_and_title);
    RUN_TEST(test_render_highlights_selection_when_focused);
    RUN_TEST(test_render_does_not_highlight_when_unfocused);
    RUN_TEST(test_render_bolds_unread_rows);
    RUN_TEST(test_render_respects_scroll_top);
    RUN_TEST(test_render_null_args_are_noops);
}
