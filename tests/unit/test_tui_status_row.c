/**
 * @file tests/unit/test_tui_status_row.c
 * @brief Unit tests for the status row (US-11 v2).
 */

#include "test_helpers.h"
#include "tui/status_row.h"

#include <string.h>

static void row_text(const Screen *s, int row, char *out, size_t cap) {
    size_t oi = 0;
    for (int c = 0; c < s->cols && oi + 1 < cap; c++) {
        ScreenCell cell = s->back[row * s->cols + c];
        out[oi++] = (cell.cp && cell.cp < 128) ? (char)cell.cp : ' ';
    }
    out[oi] = '\0';
}

static void test_init_is_dialogs_mode(void) {
    StatusRow sr; status_row_init(&sr);
    ASSERT(sr.mode == STATUS_MODE_DIALOGS, "default mode is dialogs");
    ASSERT(sr.message[0] == '\0', "empty message");
}

static void test_set_message_truncates(void) {
    StatusRow sr; status_row_init(&sr);
    char big[STATUS_ROW_MSG_MAX + 50];
    memset(big, 'x', sizeof(big));
    big[sizeof(big) - 1] = '\0';
    status_row_set_message(&sr, big);
    ASSERT(strlen(sr.message) == STATUS_ROW_MSG_MAX - 1, "truncated");
    status_row_set_message(&sr, NULL);
    ASSERT(sr.message[0] == '\0', "null clears");
}

static void test_render_dialogs_mode_shows_hint(void) {
    Screen s; ASSERT(screen_init(&s, 1, 80) == 0, "init");
    StatusRow sr; status_row_init(&sr);
    Pane p = { .row = 0, .col = 0, .rows = 1, .cols = 80 };
    status_row_render(&sr, &p, &s);
    char buf[128]; row_text(&s, 0, buf, sizeof(buf));
    ASSERT(strstr(buf, "[dialogs]") != NULL, "dialogs prefix");
    ASSERT(strstr(buf, "Enter") != NULL, "enter hint");
    /* Every cell on the row must have SCREEN_ATTR_REVERSE set. */
    for (int c = 0; c < 80; c++) {
        ASSERT(s.back[c].attrs & SCREEN_ATTR_REVERSE, "full-row reverse");
    }
    screen_free(&s);
}

static void test_render_history_mode_shows_different_hint(void) {
    Screen s; ASSERT(screen_init(&s, 1, 80) == 0, "init");
    StatusRow sr; status_row_init(&sr);
    sr.mode = STATUS_MODE_HISTORY;
    Pane p = { .row = 0, .col = 0, .rows = 1, .cols = 80 };
    status_row_render(&sr, &p, &s);
    char buf[128]; row_text(&s, 0, buf, sizeof(buf));
    ASSERT(strstr(buf, "[history]") != NULL, "history prefix");
    ASSERT(strstr(buf, "Tab") != NULL, "tab hint");
    screen_free(&s);
}

static void test_render_message_right_aligned(void) {
    Screen s; ASSERT(screen_init(&s, 1, 80) == 0, "init");
    StatusRow sr; status_row_init(&sr);
    status_row_set_message(&sr, "loading");
    Pane p = { .row = 0, .col = 0, .rows = 1, .cols = 80 };
    status_row_render(&sr, &p, &s);
    /* "loading" is 7 chars; col = 80 - 7 - 1 = 72 */
    ASSERT(s.back[72].cp == 'l', "message starts at right-aligned col");
    ASSERT(s.back[78].cp == 'g', "message ends near right edge");
    screen_free(&s);
}

static void test_render_null_args_noop(void) {
    Screen s; ASSERT(screen_init(&s, 1, 20) == 0, "init");
    Pane p = { .row = 0, .col = 0, .rows = 1, .cols = 20 };
    StatusRow sr; status_row_init(&sr);
    status_row_render(NULL, &p, &s);
    status_row_render(&sr, NULL, &s);
    status_row_render(&sr, &p, NULL);
    ASSERT(s.back[0].cp == ' ', "back untouched");
    screen_free(&s);
}

void test_tui_status_row_run(void) {
    RUN_TEST(test_init_is_dialogs_mode);
    RUN_TEST(test_set_message_truncates);
    RUN_TEST(test_render_dialogs_mode_shows_hint);
    RUN_TEST(test_render_history_mode_shows_different_hint);
    RUN_TEST(test_render_message_right_aligned);
    RUN_TEST(test_render_null_args_noop);
}
