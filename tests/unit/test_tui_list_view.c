/**
 * @file tests/unit/test_tui_list_view.c
 * @brief Unit tests for the scroll/selection helper (US-11 v2).
 */

#include "test_helpers.h"
#include "tui/list_view.h"

static void test_init_is_empty(void) {
    ListView lv;
    list_view_init(&lv);
    ASSERT(lv.count == 0, "count 0");
    ASSERT(lv.selected == -1, "selected -1");
    ASSERT(lv.scroll_top == 0, "scroll_top 0");
    ASSERT(lv.rows_visible == 0, "rows_visible 0");
}

static void test_set_count_selects_first_when_unset(void) {
    ListView lv; list_view_init(&lv);
    list_view_set_viewport(&lv, 5);
    list_view_set_count(&lv, 10);
    ASSERT(lv.selected == 0, "first item selected");
    ASSERT(lv.scroll_top == 0, "at top");
}

static void test_set_count_clamps_selected(void) {
    ListView lv; list_view_init(&lv);
    list_view_set_viewport(&lv, 5);
    list_view_set_count(&lv, 10);
    lv.selected = 7;
    lv.scroll_top = 5;
    list_view_set_count(&lv, 4);
    ASSERT(lv.selected == 3, "selection clamped to last valid");
    ASSERT(lv.scroll_top <= 3, "scroll clamped");
}

static void test_set_count_zero_resets_selection(void) {
    ListView lv; list_view_init(&lv);
    list_view_set_viewport(&lv, 5);
    list_view_set_count(&lv, 10);
    list_view_set_count(&lv, 0);
    ASSERT(lv.selected == -1, "empty list has no selection");
    ASSERT(lv.scroll_top == 0, "scroll reset");
}

static void test_move_down_advances_and_scrolls(void) {
    ListView lv; list_view_init(&lv);
    list_view_set_viewport(&lv, 3);
    list_view_set_count(&lv, 10);
    for (int i = 0; i < 3; i++) list_view_move_down(&lv);
    ASSERT(lv.selected == 3, "selected = 3");
    ASSERT(lv.scroll_top == 1, "scroll followed selection");
    ASSERT(list_view_is_visible(&lv, 3), "selection visible");
}

static void test_move_down_stops_at_last(void) {
    ListView lv; list_view_init(&lv);
    list_view_set_viewport(&lv, 5);
    list_view_set_count(&lv, 3);
    for (int i = 0; i < 10; i++) list_view_move_down(&lv);
    ASSERT(lv.selected == 2, "pinned at last");
}

static void test_move_up_stops_at_first(void) {
    ListView lv; list_view_init(&lv);
    list_view_set_viewport(&lv, 3);
    list_view_set_count(&lv, 10);
    for (int i = 0; i < 5; i++) list_view_move_down(&lv);
    for (int i = 0; i < 10; i++) list_view_move_up(&lv);
    ASSERT(lv.selected == 0, "pinned at first");
    ASSERT(lv.scroll_top == 0, "scroll back to top");
}

static void test_page_down_jumps_by_viewport(void) {
    ListView lv; list_view_init(&lv);
    list_view_set_viewport(&lv, 4);
    list_view_set_count(&lv, 20);
    list_view_page_down(&lv);
    ASSERT(lv.selected == 4, "selected jumped by 4");
    list_view_page_down(&lv);
    ASSERT(lv.selected == 8, "second page");
    ASSERT(list_view_is_visible(&lv, 8), "selection in viewport after page");
}

static void test_page_up_jumps_back(void) {
    ListView lv; list_view_init(&lv);
    list_view_set_viewport(&lv, 4);
    list_view_set_count(&lv, 20);
    lv.selected = 12;
    lv.scroll_top = 9;
    list_view_page_up(&lv);
    ASSERT(lv.selected == 8, "selected -= viewport");
}

static void test_home_end(void) {
    ListView lv; list_view_init(&lv);
    list_view_set_viewport(&lv, 5);
    list_view_set_count(&lv, 30);
    list_view_end(&lv);
    ASSERT(lv.selected == 29, "end selects last");
    ASSERT(list_view_is_visible(&lv, 29), "last visible");
    list_view_home(&lv);
    ASSERT(lv.selected == 0, "home selects first");
    ASSERT(lv.scroll_top == 0, "scroll to top");
}

static void test_reveal_keeps_selection_in_view(void) {
    ListView lv; list_view_init(&lv);
    list_view_set_viewport(&lv, 5);
    list_view_set_count(&lv, 30);
    lv.selected = 20;
    list_view_reveal_selected(&lv);
    ASSERT(list_view_is_visible(&lv, 20), "selection revealed");
    ASSERT(lv.scroll_top == 16, "minimum scroll for visibility");
}

static void test_set_viewport_reveals(void) {
    ListView lv; list_view_init(&lv);
    list_view_set_viewport(&lv, 10);
    list_view_set_count(&lv, 50);
    lv.selected = 40;
    list_view_reveal_selected(&lv);
    ASSERT(lv.scroll_top == 31, "scroll matches big viewport");
    /* Shrinking the viewport must keep selection visible. */
    list_view_set_viewport(&lv, 3);
    ASSERT(lv.rows_visible == 3, "viewport shrunk");
    ASSERT(list_view_is_visible(&lv, 40), "selection still visible");
}

static void test_move_on_empty_is_noop(void) {
    ListView lv; list_view_init(&lv);
    list_view_move_up(&lv);
    list_view_move_down(&lv);
    list_view_page_up(&lv);
    list_view_page_down(&lv);
    list_view_home(&lv);
    list_view_end(&lv);
    ASSERT(lv.selected == -1, "empty navigation left alone");
    ASSERT(lv.count == 0, "still empty");
}

static void test_is_visible_rejects_out_of_range(void) {
    ListView lv; list_view_init(&lv);
    list_view_set_viewport(&lv, 3);
    list_view_set_count(&lv, 5);
    ASSERT(list_view_is_visible(&lv, 0), "first visible");
    ASSERT(list_view_is_visible(&lv, 2), "last of viewport visible");
    ASSERT(!list_view_is_visible(&lv, 3), "row 3 not visible");
}

void test_tui_list_view_run(void) {
    RUN_TEST(test_init_is_empty);
    RUN_TEST(test_set_count_selects_first_when_unset);
    RUN_TEST(test_set_count_clamps_selected);
    RUN_TEST(test_set_count_zero_resets_selection);
    RUN_TEST(test_move_down_advances_and_scrolls);
    RUN_TEST(test_move_down_stops_at_last);
    RUN_TEST(test_move_up_stops_at_first);
    RUN_TEST(test_page_down_jumps_by_viewport);
    RUN_TEST(test_page_up_jumps_back);
    RUN_TEST(test_home_end);
    RUN_TEST(test_reveal_keeps_selection_in_view);
    RUN_TEST(test_set_viewport_reveals);
    RUN_TEST(test_move_on_empty_is_noop);
    RUN_TEST(test_is_visible_rejects_out_of_range);
}
