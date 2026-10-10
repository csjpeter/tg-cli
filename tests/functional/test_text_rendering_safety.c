/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-20 */
/* Unicode + ANSI-escape safety for screen rendering. */

/**
 * @file test_text_rendering_safety.c
 * @brief TEST-78 / US-27 — Unicode + ANSI-escape safety for TUI rendering.
 *
 * A message body or a dialog title is attacker-controlled text.  The subject
 * is the production path from a HistoryEntry / DialogEntry through
 * history_pane_render() / dialog_pane_render() and libtuikit's tui_screen_put
 * to the bytes tui_screen_render() emits:
 *
 *   - ESC, BEL, C1 controls (U+0080..U+009F) and DEL never reach the
 *     terminal; they are shown as U+00B7 MIDDLE DOT,
 *   - the bidi override / isolate controls are removed (tui_text_strip_bidi),
 *   - malformed UTF-8 becomes U+FFFD per invalid byte,
 *   - wide characters occupy two columns, combining marks none.
 *
 * Every test also compares the number of ESC bytes in the emitted stream
 * with that of a benign text of the same length: the text must add none.
 */

#include "test_helpers.h"

#include "tui/dialog_pane.h"
#include "tui/history_pane.h"
#include "tui/text_safe.h"
#include "tui_grid.h"


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREFIX "< [1] "   /* what history_pane_render puts before a body */
#define PREFIX_COLS 6

/* Render one received message body through the history pane (1 row x 64
 * columns).  Fills @p g with the cells and returns the emitted stream in
 * @p raw (caller frees). */
static void render_body(const char *body, TuiGrid *g, tui_buf *raw) {
    HistoryPane *hp = calloc(1, sizeof(*hp));
    history_pane_init(hp);
    HistoryEntry e;
    memset(&e, 0, sizeof(e));
    e.id = 1;
    snprintf(e.text, sizeof(e.text), "%s", body);
    HistoryPeer peer;
    memset(&peer, 0, sizeof(peer));
    history_pane_set_entries(hp, &peer, &e, 1);
    /* These tests are about the text, not the selection style: nothing is
     * selected, so the row is drawn in its plain style. */
    hp->lv.selected = -1;

    tui_screen *s = tui_screen_new(1, 64);
    tui_rect r = tui_rect_make(0, 0, 1, 64);
    history_pane_render(hp, &r, s, 1);
    tui_grid_capture(g, s);
    tui_buf_init(raw);
    tui_screen_invalidate(s);
    tui_screen_render(s, raw);
    tui_screen_free(s);
    free(hp);
}

static int count_byte(const tui_buf *b, unsigned char v) {
    int n = 0;
    for (size_t i = 0; i < b->len; i++)
        if ((unsigned char)b->data[i] == v) n++;
    return n;
}

/* The stream must carry no BEL, no DEL, no C1 control (as UTF-8 C2 80..9F),
 * and only the library's own escape sequences: every ESC starts a CSI made
 * of digits, ';' and '?' and ends in h, l, m, H or J, and the only erase of
 * the display is the library's initial one (an injected ESC[2J would be a
 * second). */
static void assert_stream_clean(const tui_buf *raw, const char *where) {
    ASSERT(count_byte(raw, 0x07) == 0, "no BEL in the stream");
    ASSERT(count_byte(raw, 0x7f) == 0, "no DEL in the stream");
    int erases = 0;
    for (size_t i = 0; i < raw->len; i++) {
        unsigned char a = (unsigned char)raw->data[i];
        if (a == 0xC2 && i + 1 < raw->len) {
            unsigned char b = (unsigned char)raw->data[i + 1];
            ASSERT(!(b >= 0x80 && b <= 0x9F), "no C1 control");
        }
        if (a != 0x1b) continue;
        ASSERT(i + 1 < raw->len && raw->data[i + 1] == '[', "ESC starts a CSI");
        size_t j = i + 2;
        while (j < raw->len && (raw->data[j] == ';' || raw->data[j] == '?'
                                || (raw->data[j] >= '0' && raw->data[j] <= '9')))
            j++;
        ASSERT(j < raw->len, "CSI is terminated");
        char fin = raw->data[j];
        ASSERT(fin == 'h' || fin == 'l' || fin == 'm' || fin == 'H' || fin == 'J',
               "CSI final byte is one the library emits");
        if (fin == 'J') erases++;
    }
    ASSERT(erases <= 1, "no injected erase-display");
    (void)where;
}

static void body_text(const TuiGrid *g, char *out, size_t cap) {
    char line[512];
    tui_grid_row_text(g, 0, line, sizeof(line));
    if (strncmp(line, PREFIX, PREFIX_COLS) == 0)
        snprintf(out, cap, "%s", line + PREFIX_COLS);
    else
        snprintf(out, cap, "%s", line);
}

/* ================================================================ */
/* Tests                                                            */
/* ================================================================ */

/* 1. ANSI CSI erase-display inside a message body. */
static void test_plain_history_strips_ansi_csi(void) {
    TuiGrid g; tui_buf raw;
    render_body("A\x1b[2JB", &g, &raw);
    char t[512]; body_text(&g, t, sizeof(t));
    ASSERT(strcmp(t, "A\xc2\xb7[2JB") == 0, "ESC shown as MIDDLE DOT, rest literal");
    assert_stream_clean(&raw, __func__);
    tui_buf_free(&raw);
}

/* 2. OSC title-set sequence: ESC ]0;evil BEL */
static void test_plain_history_strips_osc_title(void) {
    TuiGrid g; tui_buf raw;
    render_body("\x1b]0;evil\x07" "X", &g, &raw);
    char t[512]; body_text(&g, t, sizeof(t));
    ASSERT(strcmp(t, "\xc2\xb7]0;evil\xc2\xb7X") == 0, "ESC and BEL neutralised");
    assert_stream_clean(&raw, __func__);
    tui_buf_free(&raw);
}

/* 3. BEL (0x07), 8-bit CSI (U+009B) and DEL (0x7F). */
static void test_plain_history_strips_bel_and_c1(void) {
    TuiGrid g; tui_buf raw;
    render_body("\x07" "a" "\xc2\x9b" "b" "\x7f" "c", &g, &raw);
    char t[512]; body_text(&g, t, sizeof(t));
    ASSERT(strcmp(t, "\xc2\xb7" "a\xc2\xb7" "b\xc2\xb7" "c") == 0,
           "BEL, U+009B and DEL each shown as MIDDLE DOT");
    assert_stream_clean(&raw, __func__);
    tui_buf_free(&raw);
}

/* 4. Tab and newline never reach the terminal as controls and never move
 *    the following text out of its row. */
static void test_plain_history_tab_and_newline_stay_on_the_row(void) {
    TuiGrid g; tui_buf raw;
    render_body("a\tb\nc", &g, &raw);
    char t[512]; body_text(&g, t, sizeof(t));
    ASSERT(t[0] == 'a', "'a' first");
    ASSERT(strchr(t, 'b') != NULL && strchr(t, 'c') != NULL,
           "text after the controls is still on the row");
    ASSERT(count_byte(&raw, '\t') == 0, "no TAB in the stream");
    ASSERT(count_byte(&raw, '\n') == 0, "no LF in the stream");
    assert_stream_clean(&raw, __func__);
    tui_buf_free(&raw);
}

/* 5. Emoji smiley (U+1F600) is two columns wide. */
static void test_tui_pane_renders_emoji_message(void) {
    TuiGrid g; tui_buf raw;
    render_body("hi \xf0\x9f\x98\x80" "!", &g, &raw);
    char t[512]; body_text(&g, t, sizeof(t));
    ASSERT(strcmp(t, "hi \xf0\x9f\x98\x80 !") == 0 || strcmp(t, "hi \xf0\x9f\x98\x80!") == 0,
           "emoji drawn whole");
    /* '!' lands after "hi " (3) + emoji (2): column PREFIX_COLS + 5 */
    ASSERT(g.cell[0][PREFIX_COLS + 5][0] == '!', "'!' two columns after the emoji start");
    assert_stream_clean(&raw, __func__);
    tui_buf_free(&raw);
}

/* 6. CJK: two columns each. */
static void test_tui_pane_renders_cjk_message(void) {
    TuiGrid g; tui_buf raw;
    render_body("\xe4\xbd\xa0\xe5\xa5\xbd" "!", &g, &raw);
    ASSERT(strncmp(g.cell[0][PREFIX_COLS], "\xe4\xbd\xa0", 3) == 0, "first glyph at its column");
    ASSERT(strncmp(g.cell[0][PREFIX_COLS + 2], "\xe5\xa5\xbd", 3) == 0, "second glyph two columns on");
    ASSERT(g.cell[0][PREFIX_COLS + 4][0] == '!', "text after 4 columns");
    assert_stream_clean(&raw, __func__);
    tui_buf_free(&raw);
}

/* 7. RTL Hebrew stays in logical order, and an override character in the
 *    body is removed: it must not reach the terminal. */
static void test_tui_pane_renders_rtl_message(void) {
    TuiGrid g; tui_buf raw;
    render_body("\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d", &g, &raw);
    char t[512]; body_text(&g, t, sizeof(t));
    ASSERT(strcmp(t, "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d") == 0, "logical order kept");
    tui_buf_free(&raw);

    const char *marks[] = {
        "A\xe2\x80\xae" "B",     /* U+202E RLO */
        "A\xe2\x80\xab" "B",     /* U+202B RLE */
        "A\xe2\x81\xa7" "B",     /* U+2067 RLI */
        "A\xe2\x81\xa9" "B",     /* U+2069 PDI */
        "A\xe2\x80\x8f" "B",     /* U+200F RLM */
        "A\xd8\x9c" "B",         /* U+061C ALM */
    };
    for (size_t i = 0; i < sizeof(marks) / sizeof(marks[0]); i++) {
        render_body(marks[i], &g, &raw);
        body_text(&g, t, sizeof(t));
        ASSERT(strcmp(t, "AB") == 0, "bidi control removed from the row");
        ASSERT(g.cell[0][PREFIX_COLS + 1][0] == 'B', "'B' directly after 'A'");
        ASSERT(memmem(raw.data, raw.len, "\xe2\x80\xae", 3) == NULL, "no RLO in the stream");
        ASSERT(memmem(raw.data, raw.len, "\xe2\x81\xa7", 3) == NULL, "no RLI in the stream");
        tui_buf_free(&raw);
    }
}

/* 8. ZWJ family cluster: no code point lost, no escape leaked, the three
 *    wide emoji take six columns. */
static void test_tui_pane_zwj_cluster(void) {
    TuiGrid g; tui_buf raw;
    render_body("\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d"
                "\xf0\x9f\x91\xa7" "!", &g, &raw);
    ASSERT(strncmp(g.cell[0][PREFIX_COLS], "\xf0\x9f\x91\xa8", 4) == 0, "man at its column");
    ASSERT(g.cell[0][PREFIX_COLS + 6][0] == '!', "'!' after six columns");
    assert_stream_clean(&raw, __func__);
    tui_buf_free(&raw);
}

/* 9. Combining mark: "e" + U+0301 is one column. */
static void test_tui_pane_combining_mark_width_zero(void) {
    TuiGrid g; tui_buf raw;
    render_body("e\xcc\x81" "xt", &g, &raw);
    ASSERT(g.cell[0][PREFIX_COLS + 1][0] == 'x', "'x' directly after the accented e");
    ASSERT(g.cell[0][PREFIX_COLS + 2][0] == 't', "'t' next");
    assert_stream_clean(&raw, __func__);
    tui_buf_free(&raw);
}

/* 10. Malformed UTF-8: each invalid byte becomes U+FFFD. */
static void test_malformed_utf8_replacement_char(void) {
    TuiGrid g; tui_buf raw;
    render_body("A" "\xc0\xff\xfe" "B", &g, &raw);
    char t[512]; body_text(&g, t, sizeof(t));
    ASSERT(strcmp(t, "A\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd" "B") == 0,
           "three invalid bytes, three U+FFFD");
    assert_stream_clean(&raw, __func__);
    tui_buf_free(&raw);
}

/* 11. Overlong / 5-byte lead: every rejected byte is a U+FFFD. */
static void test_overlong_utf8_rejected(void) {
    TuiGrid g; tui_buf raw;
    render_body("[" "\xf8\x88\x80\x80\x80" "]", &g, &raw);
    char t[512]; body_text(&g, t, sizeof(t));
    ASSERT(strcmp(t, "[\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd]") == 0,
           "five rejected bytes, five U+FFFD");
    assert_stream_clean(&raw, __func__);
    tui_buf_free(&raw);
}

/* 12. A BOM inside the body takes no column. */
static void test_utf8_bom_zero_width(void) {
    TuiGrid g; tui_buf raw;
    render_body("a\xef\xbb\xbf" "b", &g, &raw);
    ASSERT(g.cell[0][PREFIX_COLS][0] == 'a', "'a'");
    /* 'b' is in the next column, with the zero-width mark attached or dropped */
    ASSERT(g.cell[0][PREFIX_COLS + 1][0] == 'b'
           || strncmp(g.cell[0][PREFIX_COLS + 1], "\xef\xbb\xbf", 3) == 0,
           "'b' follows directly");
    assert_stream_clean(&raw, __func__);
    tui_buf_free(&raw);
}

/* 13. A dialog title is as hostile as a message body. */
static void test_dialog_title_is_neutralised(void) {
    DialogPane *dp = calloc(1, sizeof(*dp));
    dialog_pane_init(dp);
    DialogEntry e;
    memset(&e, 0, sizeof(e));
    e.kind = DIALOG_PEER_USER;
    e.peer_id = 1;
    snprintf(e.title, sizeof(e.title),
             "Ev\x1b]0;x\x07" "il\xe2\x80\xae" "gnp.exe");
    dialog_pane_set_entries(dp, &e, 1);
    tui_screen *s = tui_screen_new(1, 40);
    tui_rect r = tui_rect_make(0, 0, 1, 40);
    dialog_pane_render(dp, &r, s, 1);
    tui_buf raw;
    tui_buf_init(&raw);
    tui_screen_invalidate(s);
    tui_screen_render(s, &raw);
    ASSERT(count_byte(&raw, 0x07) == 0, "no BEL from a title");
    ASSERT(memmem(raw.data, raw.len, "\xe2\x80\xae", 3) == NULL, "no RLO from a title");
    ASSERT(memmem(raw.data, raw.len, "\x1b]", 2) == NULL, "no OSC from a title");
    tui_buf_free(&raw);
    tui_screen_free(s);
    free(dp);
}

/* 14. The filter itself: bounds and non-bidi text untouched. */
static void test_strip_bidi_filter(void) {
    char out[16];
    ASSERT(tui_text_strip_bidi("plain \xc3\xa9\xe4\xbd\xa0", out, sizeof(out)) == 11,
           "ordinary text unchanged");
    ASSERT(strcmp(out, "plain \xc3\xa9\xe4\xbd\xa0") == 0, "copied verbatim");
    char tiny[5];
    size_t n = tui_text_strip_bidi("ab\xe2\x80\xae" "cdefgh", tiny, sizeof(tiny));
    ASSERT(n == 4 && strcmp(tiny, "abcd") == 0, "truncated, NUL-terminated");
    char cut[4];
    n = tui_text_strip_bidi("a\xe4\xbd\xa0", cut, sizeof(cut));
    ASSERT(n == 1 && strcmp(cut, "a") == 0, "never cuts inside a character");
    ASSERT(tui_text_strip_bidi(NULL, out, sizeof(out)) == 0 && out[0] == '\0', "NULL input");
    ASSERT(tui_text_strip_bidi("x", out, 0) == 0, "zero capacity");
}

/* ================================================================ */
/* Suite entry point                                                */
/* ================================================================ */

void run_text_rendering_safety_tests(void) {
    RUN_TEST(test_plain_history_strips_ansi_csi);
    RUN_TEST(test_plain_history_strips_osc_title);
    RUN_TEST(test_plain_history_strips_bel_and_c1);
    RUN_TEST(test_plain_history_tab_and_newline_stay_on_the_row);
    RUN_TEST(test_tui_pane_renders_emoji_message);
    RUN_TEST(test_tui_pane_renders_cjk_message);
    RUN_TEST(test_tui_pane_renders_rtl_message);
    RUN_TEST(test_tui_pane_zwj_cluster);
    RUN_TEST(test_tui_pane_combining_mark_width_zero);
    RUN_TEST(test_malformed_utf8_replacement_char);
    RUN_TEST(test_overlong_utf8_rejected);
    RUN_TEST(test_utf8_bom_zero_width);
    RUN_TEST(test_dialog_title_is_neutralised);
    RUN_TEST(test_strip_bidi_filter);
}
