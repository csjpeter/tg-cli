/**
 * @file tests/unit/test_tui_history_pane.c
 * @brief Unit tests for the history pane view-model (US-11 v2).
 */

#include "test_helpers.h"
#include "tui/history_pane.h"

#include <string.h>

/* --- Helpers --- */

static HistoryEntry mk_text(int32_t id, int outgoing, const char *text) {
    HistoryEntry e = {0};
    e.id = id;
    e.out = outgoing;
    if (text) {
        strncpy(e.text, text, HISTORY_TEXT_MAX - 1);
        e.text[HISTORY_TEXT_MAX - 1] = '\0';
    }
    return e;
}

static HistoryEntry mk_complex(int32_t id) {
    HistoryEntry e = {0};
    e.id = id;
    e.complex = 1;
    return e;
}

static HistoryEntry mk_media(int32_t id, MediaKind kind) {
    HistoryEntry e = {0};
    e.id = id;
    e.media = kind;
    return e;
}

static HistoryPeer mk_peer_self(void) {
    HistoryPeer p = { .kind = HISTORY_PEER_SELF };
    return p;
}

static void row_text(const Screen *s, int row, char *out, size_t cap) {
    size_t oi = 0;
    for (int c = 0; c < s->cols && oi + 1 < cap; c++) {
        ScreenCell cell = s->back[row * s->cols + c];
        out[oi++] = (cell.cp && cell.cp < 128) ? (char)cell.cp : ' ';
    }
    while (oi > 0 && out[oi - 1] == ' ') oi--;
    out[oi] = '\0';
}

/* --- State tests --- */

static void test_init_is_empty_and_unloaded(void) {
    HistoryPane hp;
    history_pane_init(&hp);
    ASSERT(hp.count == 0, "no entries");
    ASSERT(hp.peer_loaded == 0, "not yet loaded");
    ASSERT(hp.lv.selected == -1, "no selection");
}

static void test_set_entries_marks_loaded(void) {
    HistoryPane hp; history_pane_init(&hp);
    HistoryPeer p = mk_peer_self();
    HistoryEntry src[2] = { mk_text(10, 1, "hi"), mk_text(9, 0, "hey") };
    history_pane_set_entries(&hp, &p, src, 2);
    ASSERT(hp.peer_loaded == 1, "loaded");
    ASSERT(hp.count == 2, "2 entries");
    ASSERT(hp.peer.kind == HISTORY_PEER_SELF, "peer copied");
    ASSERT(hp.lv.selected == 0, "selection at top");
}

static void test_set_entries_clamps_overflow(void) {
    HistoryPane hp; history_pane_init(&hp);
    HistoryPeer p = mk_peer_self();
    /* src must be at least HISTORY_PANE_MAX+1 so the clamp fires without
     * reading beyond the array bounds. */
    HistoryEntry src[HISTORY_PANE_MAX + 1];
    memset(src, 0, sizeof(src));
    for (int i = 0; i < HISTORY_PANE_MAX + 1; i++)
        src[i] = mk_text(i + 1, 0, "x");
    history_pane_set_entries(&hp, &p, src, HISTORY_PANE_MAX + 1);
    ASSERT(hp.count == HISTORY_PANE_MAX, "clamped to max");
    /* Setting count=0 should keep peer_loaded=1 but clear the entries. */
    history_pane_set_entries(&hp, &p, NULL, 0);
    ASSERT(hp.count == 0, "reset to empty");
    ASSERT(hp.peer_loaded == 1, "peer still loaded");
    ASSERT(hp.lv.selected == -1, "no selection on empty");
}

/* --- Render tests --- */

static void test_render_unloaded_shows_select_hint(void) {
    Screen s; ASSERT(screen_init(&s, 5, 25) == 0, "init screen");
    HistoryPane hp; history_pane_init(&hp);
    Pane p = { .row = 0, .col = 0, .rows = 5, .cols = 25 };
    hp.lv.rows_visible = p.rows;
    history_pane_render(&hp, &p, &s);
    char buf[64]; row_text(&s, 2, buf, sizeof(buf));
    ASSERT(strstr(buf, "(select a dialog)") != NULL, "hint shown");
    screen_free(&s);
}

static void test_render_loaded_empty_shows_no_messages(void) {
    Screen s; ASSERT(screen_init(&s, 5, 25) == 0, "init screen");
    HistoryPane hp; history_pane_init(&hp);
    HistoryPeer peer = mk_peer_self();
    history_pane_set_entries(&hp, &peer, NULL, 0);
    Pane p = { .row = 0, .col = 0, .rows = 5, .cols = 25 };
    hp.lv.rows_visible = p.rows;
    history_pane_render(&hp, &p, &s);
    char buf[64]; row_text(&s, 2, buf, sizeof(buf));
    ASSERT(strstr(buf, "(no messages)") != NULL, "empty placeholder");
    screen_free(&s);
}

static void test_render_rows_show_direction_and_text(void) {
    Screen s; ASSERT(screen_init(&s, 4, 30) == 0, "init screen");
    HistoryPane hp; history_pane_init(&hp);
    HistoryPeer peer = mk_peer_self();
    HistoryEntry src[3] = {
        mk_text(100, 1, "hello"),   /* outgoing */
        mk_text(99, 0, "hi there"), /* incoming */
        mk_complex(98),
    };
    history_pane_set_entries(&hp, &peer, src, 3);
    Pane p = { .row = 0, .col = 0, .rows = 4, .cols = 30 };
    hp.lv.rows_visible = p.rows;
    history_pane_render(&hp, &p, &s);
    char row0[64]; row_text(&s, 0, row0, sizeof(row0));
    char row1[64]; row_text(&s, 1, row1, sizeof(row1));
    char row2[64]; row_text(&s, 2, row2, sizeof(row2));
    ASSERT(row0[0] == '>', "outgoing arrow");
    ASSERT(strstr(row0, "[100]") != NULL, "id badge");
    ASSERT(strstr(row0, "hello") != NULL, "text rendered");
    ASSERT(row1[0] == '<', "incoming arrow");
    ASSERT(strstr(row2, "(complex)") != NULL, "complex marker");
    screen_free(&s);
}

static void test_render_media_without_text_shows_media_marker(void) {
    Screen s; ASSERT(screen_init(&s, 2, 30) == 0, "init screen");
    HistoryPane hp; history_pane_init(&hp);
    HistoryPeer peer = mk_peer_self();
    HistoryEntry src[1] = { mk_media(50, MEDIA_PHOTO) };
    history_pane_set_entries(&hp, &peer, src, 1);
    Pane p = { .row = 0, .col = 0, .rows = 2, .cols = 30 };
    hp.lv.rows_visible = p.rows;
    history_pane_render(&hp, &p, &s);
    char row[64]; row_text(&s, 0, row, sizeof(row));
    ASSERT(strstr(row, "(media)") != NULL, "media marker");
    screen_free(&s);
}

static void test_render_respects_scroll(void) {
    Screen s; ASSERT(screen_init(&s, 3, 30) == 0, "init screen");
    HistoryPane hp; history_pane_init(&hp);
    HistoryPeer peer = mk_peer_self();
    HistoryEntry src[6];
    for (int i = 0; i < 6; i++) {
        char text[8]; snprintf(text, sizeof(text), "msg%d", i);
        src[i] = mk_text(1000 + i, 0, text);
    }
    history_pane_set_entries(&hp, &peer, src, 6);
    Pane p = { .row = 0, .col = 0, .rows = 3, .cols = 30 };
    hp.lv.rows_visible = p.rows;
    list_view_end(&hp.lv);  /* scroll to bottom */
    history_pane_render(&hp, &p, &s);
    char row0[64]; row_text(&s, 0, row0, sizeof(row0));
    char row2[64]; row_text(&s, 2, row2, sizeof(row2));
    ASSERT(strstr(row0, "msg3") != NULL, "row 0 shows msg3 after scroll");
    ASSERT(strstr(row2, "msg5") != NULL, "row 2 shows last message");
    screen_free(&s);
}

static void test_render_complex_row_is_dimmed(void) {
    Screen s; ASSERT(screen_init(&s, 2, 20) == 0, "init screen");
    HistoryPane hp; history_pane_init(&hp);
    HistoryPeer peer = mk_peer_self();
    HistoryEntry src[1] = { mk_complex(42) };
    history_pane_set_entries(&hp, &peer, src, 1);
    Pane p = { .row = 0, .col = 0, .rows = 2, .cols = 20 };
    hp.lv.rows_visible = p.rows;
    history_pane_render(&hp, &p, &s);
    ASSERT(s.back[0].attrs & SCREEN_ATTR_DIM, "complex row is dimmed");
    screen_free(&s);
}

static void test_render_null_args_noop(void) {
    Screen s; ASSERT(screen_init(&s, 2, 10) == 0, "init screen");
    Pane p = { .row = 0, .col = 0, .rows = 2, .cols = 10 };
    HistoryPane hp; history_pane_init(&hp);
    history_pane_render(NULL, &p, &s);
    history_pane_render(&hp, NULL, &s);
    history_pane_render(&hp, &p, NULL);
    ASSERT(s.back[0].cp == ' ', "back untouched");
    screen_free(&s);
}

void test_tui_history_pane_run(void) {
    RUN_TEST(test_init_is_empty_and_unloaded);
    RUN_TEST(test_set_entries_marks_loaded);
    RUN_TEST(test_set_entries_clamps_overflow);
    RUN_TEST(test_render_unloaded_shows_select_hint);
    RUN_TEST(test_render_loaded_empty_shows_no_messages);
    RUN_TEST(test_render_rows_show_direction_and_text);
    RUN_TEST(test_render_media_without_text_shows_media_marker);
    RUN_TEST(test_render_respects_scroll);
    RUN_TEST(test_render_complex_row_is_dimmed);
    RUN_TEST(test_render_null_args_noop);
}
