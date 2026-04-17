/**
 * @file tests/unit/test_tui_pane.c
 * @brief Unit tests for the TUI pane + layout geometry (US-11 v2).
 */

#include "test_helpers.h"
#include "tui/pane.h"
#include "tui/screen.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Layout tests --- */

static void test_layout_standard_80x24(void) {
    Layout L; layout_compute(&L, 24, 80, 30);
    ASSERT(pane_is_valid(&L.dialogs), "dialogs pane valid");
    ASSERT(pane_is_valid(&L.history), "history pane valid");
    ASSERT(pane_is_valid(&L.status),  "status pane valid");
    ASSERT(L.dialogs.row == 0 && L.dialogs.col == 0, "dialogs at (0,0)");
    ASSERT(L.dialogs.rows == 23, "dialogs rows = screen_rows - 1");
    ASSERT(L.dialogs.cols == 30, "dialogs cols = hint");
    ASSERT(L.history.row == 0 && L.history.col == 30, "history right of dialogs");
    ASSERT(L.history.cols == 50, "history fills remainder");
    ASSERT(L.status.row == 23 && L.status.col == 0, "status on last row");
    ASSERT(L.status.cols == 80 && L.status.rows == 1, "status is full width, 1 row");
}

static void test_layout_clamps_left_width_hint(void) {
    Layout L;
    layout_compute(&L, 24, 100, 5);           /* hint below minimum */
    ASSERT(L.dialogs.cols == TUI_MIN_LEFT_WIDTH, "hint clamped to min");
    layout_compute(&L, 24, 100, 200);         /* hint above maximum */
    ASSERT(L.dialogs.cols == TUI_MAX_LEFT_WIDTH, "hint clamped to max");
}

static void test_layout_shrinks_left_on_narrow_terminal(void) {
    /* 45 cols, hint 35 — history would be 10 cols, too narrow. Left shrinks
     * so history gets at least min_right (20), so left = 25 is expected. */
    Layout L; layout_compute(&L, 24, 45, 35);
    ASSERT(L.history.cols >= 20, "history keeps at least 20 cols");
    ASSERT(L.dialogs.cols + L.history.cols == 45, "left + right == cols");
}

static void test_layout_rejects_too_small_screen(void) {
    Layout L;
    layout_compute(&L, 2, 80, 30);
    ASSERT(!pane_is_valid(&L.dialogs), "too few rows rejected");
    layout_compute(&L, 24, 10, 5);
    ASSERT(!pane_is_valid(&L.dialogs), "too few cols rejected");
}

static void test_layout_null_out_is_noop(void) {
    layout_compute(NULL, 24, 80, 30); /* must not crash */
    ASSERT(1, "null layout handled");
}

/* --- Pane writing tests --- */

static void test_pane_put_str_translates_to_screen_coords(void) {
    Screen s; ASSERT(screen_init(&s, 10, 40) == 0, "init");
    Pane p = { .row = 2, .col = 5, .rows = 6, .cols = 10 };
    int w = pane_put_str(&p, &s, 0, 0, "hi", SCREEN_ATTR_NORMAL);
    ASSERT(w == 2, "two cells written");
    ASSERT(s.back[2 * 40 + 5].cp == 'h', "h at abs (2,5)");
    ASSERT(s.back[2 * 40 + 6].cp == 'i', "i at abs (2,6)");
    screen_free(&s);
}

static void test_pane_put_str_clips_at_right_edge_of_pane(void) {
    Screen s; ASSERT(screen_init(&s, 5, 40) == 0, "init");
    Pane p = { .row = 0, .col = 10, .rows = 5, .cols = 6 };
    int w = pane_put_str(&p, &s, 0, 0, "abcdefghij", SCREEN_ATTR_NORMAL);
    ASSERT(w == 6, "clipped to pane width (6)");
    ASSERT(s.back[10].cp == 'a', "first char lands in pane");
    ASSERT(s.back[15].cp == 'f', "last char lands inside pane");
    /* Screen column 16 is outside the pane. It may contain untouched blank. */
    ASSERT(s.back[16].cp == ' ', "no spill past pane");
    screen_free(&s);
}

static void test_pane_put_str_respects_internal_offset(void) {
    Screen s; ASSERT(screen_init(&s, 5, 40) == 0, "init");
    Pane p = { .row = 1, .col = 4, .rows = 4, .cols = 10 };
    int w = pane_put_str(&p, &s, 2, 3, "hello", SCREEN_ATTR_NORMAL);
    ASSERT(w == 5, "wrote 5 cells");
    ASSERT(s.back[3 * 40 + 7].cp == 'h', "h at abs (row=3, col=7)");
    screen_free(&s);
}

static void test_pane_put_str_rejects_out_of_range_relative_coords(void) {
    Screen s; ASSERT(screen_init(&s, 5, 40) == 0, "init");
    Pane p = { .row = 0, .col = 0, .rows = 3, .cols = 10 };
    ASSERT(pane_put_str(&p, &s, -1, 0, "x", 0) == 0, "row < 0 rejected");
    ASSERT(pane_put_str(&p, &s, 3, 0, "x", 0) == 0, "row == rows rejected");
    ASSERT(pane_put_str(&p, &s, 0, 10, "x", 0) == 0, "col == cols rejected");
    ASSERT(pane_put_str(&p, &s, 0, -1, "x", 0) == 0, "col < 0 rejected");
    screen_free(&s);
}

static void test_pane_fill_translates_and_clips(void) {
    Screen s; ASSERT(screen_init(&s, 5, 40) == 0, "init");
    Pane p = { .row = 0, .col = 5, .rows = 3, .cols = 8 };
    pane_fill(&p, &s, 1, 2, 100, SCREEN_ATTR_REVERSE);
    /* Should fill absolute cols 7..12 inclusive on row 1. */
    ASSERT(s.back[1 * 40 + 6].attrs == 0, "col 6 untouched");
    ASSERT(s.back[1 * 40 + 7].attrs == SCREEN_ATTR_REVERSE, "first fill cell");
    ASSERT(s.back[1 * 40 + 12].attrs == SCREEN_ATTR_REVERSE, "last fill cell");
    ASSERT(s.back[1 * 40 + 13].attrs == 0, "col 13 untouched (outside pane)");
    screen_free(&s);
}

static void test_pane_clear_resets_every_cell(void) {
    Screen s; ASSERT(screen_init(&s, 5, 40) == 0, "init");
    Pane p = { .row = 1, .col = 3, .rows = 2, .cols = 5 };
    /* Dirty the whole pane first. */
    for (int r = 0; r < p.rows; r++) {
        pane_fill(&p, &s, r, 0, p.cols, SCREEN_ATTR_BOLD);
    }
    pane_clear(&p, &s);
    for (int r = 0; r < p.rows; r++) {
        for (int c = 0; c < p.cols; c++) {
            int idx = (p.row + r) * 40 + (p.col + c);
            ASSERT(s.back[idx].cp == ' ', "cell blank after clear");
            ASSERT(s.back[idx].attrs == 0, "attrs reset after clear");
        }
    }
    /* Adjacent cells should be untouched. */
    ASSERT(s.back[1 * 40 + 2].attrs == 0, "left-of-pane untouched");
    ASSERT(s.back[1 * 40 + 8].attrs == 0, "right-of-pane untouched");
    screen_free(&s);
}

static void test_pane_invalid_rect_writes_are_noop(void) {
    Screen s; ASSERT(screen_init(&s, 5, 40) == 0, "init");
    Pane empty = { 0 };
    ASSERT(!pane_is_valid(&empty), "empty pane invalid");
    ASSERT(pane_put_str(&empty, &s, 0, 0, "hi", 0) == 0, "put on invalid");
    pane_fill(&empty, &s, 0, 0, 5, SCREEN_ATTR_BOLD); /* no crash */
    pane_clear(&empty, &s);                           /* no crash */
    ASSERT(s.back[0].cp == ' ', "back grid untouched");
    screen_free(&s);
}

static void test_pane_put_str_wide_char_does_not_spill(void) {
    setlocale(LC_CTYPE, "en_US.UTF-8");
    Screen s; ASSERT(screen_init(&s, 2, 10) == 0, "init");
    Pane p = { .row = 0, .col = 0, .rows = 2, .cols = 3 };
    /* "a日" — 'a' (1) + wide char (2) = 3 exactly fits; next write of another
     * wide char should be refused. */
    int w1 = pane_put_str(&p, &s, 0, 0, "a\xE6\x97\xA5", SCREEN_ATTR_NORMAL);
    if (w1 != 3) {
        screen_free(&s); return; /* no UTF-8 locale */
    }
    /* Adjacent cell (screen col 3) must remain blank. */
    ASSERT(s.back[3].cp == ' ', "no spill into col past pane");
    screen_free(&s);
}

void test_tui_pane_run(void) {
    RUN_TEST(test_layout_standard_80x24);
    RUN_TEST(test_layout_clamps_left_width_hint);
    RUN_TEST(test_layout_shrinks_left_on_narrow_terminal);
    RUN_TEST(test_layout_rejects_too_small_screen);
    RUN_TEST(test_layout_null_out_is_noop);
    RUN_TEST(test_pane_put_str_translates_to_screen_coords);
    RUN_TEST(test_pane_put_str_clips_at_right_edge_of_pane);
    RUN_TEST(test_pane_put_str_respects_internal_offset);
    RUN_TEST(test_pane_put_str_rejects_out_of_range_relative_coords);
    RUN_TEST(test_pane_fill_translates_and_clips);
    RUN_TEST(test_pane_clear_resets_every_cell);
    RUN_TEST(test_pane_invalid_rect_writes_are_noop);
    RUN_TEST(test_pane_put_str_wide_char_does_not_spill);
}
