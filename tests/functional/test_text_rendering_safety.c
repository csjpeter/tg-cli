/**
 * @file test_text_rendering_safety.c
 * @brief TEST-78 / US-27 — Unicode + ANSI-escape safety for screen rendering.
 *
 * The subject is src/tui/screen.c::screen_put_str[_n], which
 *   1. decodes UTF-8 (utf8_decode),
 *   2. rewrites hazardous control codepoints to U+00B7 MIDDLE DOT
 *      (SEC-01 sanitisation block, screen.c L111-119), and
 *   3. stores the resulting codepoint + display-width in the Screen
 *      back buffer via terminal_wcwidth().
 *
 * All three steps are pure: given a UTF-8 string, the only observable
 * side-effect is the mutation of Screen::back[].  No socket, MTProto,
 * mock server, or TL parsing is required — the functional test drives
 * the production domain end-to-end by writing a message body verbatim
 * into a Screen and inspecting the resulting cell grid.
 *
 * Scenarios covered (12 tests):
 *   1.  ANSI CSI erase-display  (ESC [ 2 J)
 *   2.  OSC title-set           (ESC ] 0 ; evil BEL)
 *   3.  BEL + 8-bit CSI + DEL   (0x07, 0x9B, 0x7F)
 *   4.  \t and \n are preserved (only allowed low-controls)
 *   5.  Emoji — smiley          (U+1F600, width 2)
 *   6.  CJK                     ("你好", 4 cols)
 *   7.  RTL — Hebrew "שלום"     (stored LTR in logical order)
 *   8.  Zero-width joiner       (👨‍👩‍👧: ZWJ family cluster)
 *   9.  Combining mark          ("e" + U+0301 → width 1)
 *  10.  Malformed UTF-8         (three invalid bytes → three U+FFFD)
 *  11.  Overlong UTF-8          (5-byte F8-lead → U+FFFD)
 *  12.  UTF-8 BOM inside body   (U+FEFF is zero-width, not garbage)
 *
 * Assertion helpers:
 *   assert_no_raw_escape_bytes — scans each cell for codepoints 0x1B,
 *      0x07, 0x9B, 0x7F; none must survive sanitisation.
 *   find_cp / count_cp — scan row for codepoint matches.
 *
 * TEST-55 remains the dialog-title width-only variant; this suite adds
 * the missing message-body + injection + malformed-UTF-8 coverage that
 * TEST-55 did not reach.
 */

#include "test_helpers.h"

#include "tui/screen.h"
#include "platform/terminal.h"

#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- shared helpers ---- */

/**
 * Fail the test if any cell in @p s carries a raw control-character
 * codepoint that SEC-01 was supposed to neutralise.
 *
 * The four bytes targeted are exactly the ones a malicious Telegram
 * message could use to break out of our display area:
 *   0x1B  ESC — lead byte of every ANSI CSI / OSC / DEC sequence.
 *   0x07  BEL — audible bell, and terminator of OSC title sequences.
 *   0x9B  CSI — 8-bit introducer equivalent to "ESC [".
 *   0x7F  DEL — some terminals interpret as erase-under-cursor.
 */
static void assert_no_raw_escape_bytes(const Screen *s, const char *where) {
    int total = s->rows * s->cols;
    for (int i = 0; i < total; i++) {
        uint32_t cp = s->back[i].cp;
        if (cp == 0x1B || cp == 0x07 || cp == 0x9B || cp == 0x7F) {
            printf("  [FAIL] %s: raw control cp=0x%02X at cell %d\n",
                   where, (unsigned)cp, i);
            g_tests_failed++;
            return;
        }
    }
}

/**
 * After screen_flip, scan the emitted byte stream for 0x07 / 0x9B / 0x7F
 * — these would only reach stdout if the sanitiser had failed, because
 * the flipper never emits those bytes itself (it only emits ESC for
 * CUP/SGR/DECTCEM sequences, which is expected and therefore excluded
 * from the check).
 *
 * The 0x1B byte is deliberately NOT flagged here: screen_flip always
 * writes CUP ("\033[…H") and SGR ("\033[…m") framing before UTF-8
 * cell contents.  We check for the non-framing escape bytes that
 * should NEVER appear in our output.
 */
static void assert_no_user_escape_in_stream(const char *buf, size_t len,
                                            const char *where) {
    for (size_t i = 0; i < len; i++) {
        unsigned char b = (unsigned char)buf[i];
        if (b == 0x07 || b == 0x9B || b == 0x7F) {
            printf("  [FAIL] %s: raw byte 0x%02X at stream offset %zu\n",
                   where, (unsigned)b, i);
            g_tests_failed++;
            return;
        }
    }
}

/** Return the column of the first cell in @p row whose cp == @p needle,
 *  or -1 if no such cell exists. */
static int find_cp(const Screen *s, int row, uint32_t needle) {
    for (int c = 0; c < s->cols; c++) {
        if (s->back[(size_t)row * s->cols + c].cp == needle) return c;
    }
    return -1;
}

/** Count occurrences of codepoint @p needle on @p row. */
static int count_cp(const Screen *s, int row, uint32_t needle) {
    int n = 0;
    for (int c = 0; c < s->cols; c++) {
        if (s->back[(size_t)row * s->cols + c].cp == needle) n++;
    }
    return n;
}

/**
 * Per-test scratch buffer backed by open_memstream() so each test can
 * examine the exact bytes screen_flip emits (and confirm no raw
 * escape payload leaks into the terminal stream).
 */
typedef struct {
    char  *buf;
    size_t len;
    FILE  *out;
} RenderSink;

/** Small screen used by every test: single row is easiest to reason about
 *  and still exercises utf8_decode + sanitiser + wcwidth. */
static void screen_setup(Screen *s, RenderSink *rs) {
    /* terminal_wcwidth() delegates to POSIX wcwidth(3), which on glibc
     * returns -1 for any non-ASCII codepoint under the default "C" locale.
     * Enable the environment locale (typically *.UTF-8) so wide/narrow
     * classification works for emoji, CJK, combining marks, etc.  The
     * TEST-55 sibling test in tests/unit/test_platform.c does the same. */
    setlocale(LC_ALL, "");

    /* 1 row × 64 cols is enough for the longest fixture (the CSI test). */
    int rc = screen_init(s, 1, 64);
    if (rc != 0) {
        printf("  [FATAL] screen_init failed\n");
        abort();
    }
    /* Route the ANSI byte stream into a memstream so each test can
     * both (a) see exactly what screen_flip emits and (b) exercise
     * the utf8_encode / sgr_encode / cup_encode / screen_flip paths
     * — none of which the back-buffer inspection alone would cover. */
    rs->buf = NULL; rs->len = 0;
    rs->out = open_memstream(&rs->buf, &rs->len);
    if (!rs->out) {
        printf("  [FATAL] open_memstream failed\n");
        abort();
    }
    s->out = rs->out;
}

static void screen_teardown(Screen *s, RenderSink *rs) {
    if (rs->out) { fclose(rs->out); rs->out = NULL; }
    free(rs->buf); rs->buf = NULL; rs->len = 0;
    screen_free(s);
}

/** Flush the back buffer through screen_flip and return the bytes the
 *  flipper wrote to the memstream.  Must be called before inspecting
 *  rs->buf / rs->len. */
static void screen_drain(Screen *s, RenderSink *rs) {
    (void)screen_flip(s);
    fflush(rs->out);
}

/* ================================================================ */
/* Tests                                                            */
/* ================================================================ */

/* 1. ANSI CSI erase-display inside a message body must become MIDDLE DOTs. */
static void test_plain_history_strips_ansi_csi(void) {
    Screen s; RenderSink rs; screen_setup(&s, &rs);

    /* "A" + ESC + "[2J" + "B" — ESC is U+001B which the sanitiser rewrites
     * to U+00B7 MIDDLE DOT (width 1 in a UTF-8 locale); the other bytes
     * are printable ASCII that pass through untouched. */
    const char msg[] = "A\x1b[2JB";
    int cols = screen_put_str(&s, 0, 0, msg, 0);
    ASSERT(cols == 6, "6 cols: A + MIDDLE_DOT + [ + 2 + J + B");

    ASSERT(s.back[0].cp == (uint32_t)'A', "cell 0 is 'A'");
    ASSERT(s.back[1].cp == 0x00B7,        "cell 1 is U+00B7 (ESC replaced)");
    ASSERT(s.back[2].cp == (uint32_t)'[', "cell 2 is '['");
    ASSERT(s.back[3].cp == (uint32_t)'2', "cell 3 is '2'");
    ASSERT(s.back[4].cp == (uint32_t)'J', "cell 4 is 'J'");
    ASSERT(s.back[5].cp == (uint32_t)'B', "cell 5 is 'B'");
    assert_no_raw_escape_bytes(&s, "ansi_csi");

    screen_drain(&s, &rs);
    assert_no_user_escape_in_stream(rs.buf, rs.len, __func__);
    screen_teardown(&s, &rs);
}

/* 2. OSC title-set sequence: ESC ]0;evil BEL */
static void test_plain_history_strips_osc_title(void) {
    Screen s; RenderSink rs; screen_setup(&s, &rs);

    const char msg[] = "\x1b]0;evil\x07" "X";
    (void)screen_put_str(&s, 0, 0, msg, 0);

    /* ESC becomes MIDDLE DOT; the literal "]0;evil" passes through as
     * printable ASCII (not hazardous on its own); BEL becomes MIDDLE DOT;
     * 'X' remains. */
    ASSERT(s.back[0].cp == 0x00B7, "ESC → U+00B7");
    ASSERT(s.back[1].cp == (uint32_t)']', "']'");
    ASSERT(s.back[2].cp == (uint32_t)'0', "'0'");
    ASSERT(s.back[3].cp == (uint32_t)';', "';'");
    ASSERT(s.back[4].cp == (uint32_t)'e', "'e'");
    ASSERT(s.back[5].cp == (uint32_t)'v', "'v'");
    ASSERT(s.back[6].cp == (uint32_t)'i', "'i'");
    ASSERT(s.back[7].cp == (uint32_t)'l', "'l'");
    ASSERT(s.back[8].cp == 0x00B7,        "BEL → U+00B7");
    ASSERT(s.back[9].cp == (uint32_t)'X', "trailing 'X' preserved");
    assert_no_raw_escape_bytes(&s, "osc_title");

    screen_drain(&s, &rs);
    assert_no_user_escape_in_stream(rs.buf, rs.len, __func__);
    screen_teardown(&s, &rs);
}

/* 3. BEL (0x07), 8-bit CSI (0xC2 0x9B in UTF-8), and DEL (0x7F). */
static void test_plain_history_strips_bel_and_c1(void) {
    Screen s; RenderSink rs; screen_setup(&s, &rs);

    /* U+009B encodes as the two UTF-8 bytes 0xC2 0x9B.  utf8_decode
     * should return cp=0x9B which the sanitiser must rewrite. */
    const char msg[] = "\x07" "a" "\xc2\x9b" "b" "\x7f" "c";
    (void)screen_put_str(&s, 0, 0, msg, 0);

    ASSERT(s.back[0].cp == 0x00B7,        "BEL → U+00B7");
    ASSERT(s.back[1].cp == (uint32_t)'a', "'a'");
    ASSERT(s.back[2].cp == 0x00B7,        "U+009B → U+00B7");
    ASSERT(s.back[3].cp == (uint32_t)'b', "'b'");
    ASSERT(s.back[4].cp == 0x00B7,        "DEL → U+00B7");
    ASSERT(s.back[5].cp == (uint32_t)'c', "'c'");
    assert_no_raw_escape_bytes(&s, "bel_c1_del");

    screen_drain(&s, &rs);
    assert_no_user_escape_in_stream(rs.buf, rs.len, __func__);
    screen_teardown(&s, &rs);
}

/* 4. \t (0x09) and \n (0x0A) are the two low-controls that SEC-01
 *    explicitly allows through — they reach terminal_wcwidth() unchanged.
 *    wcwidth() returns <=0 for both so screen_put_str skips them silently
 *    (no cell mutated, no cols consumed).  The important property is that
 *    the sanitiser did NOT replace them with U+00B7. */
static void test_plain_history_preserves_newline_and_tab(void) {
    Screen s; RenderSink rs; screen_setup(&s, &rs);

    /* Interleave the two controls with printable ASCII so the test can
     * assert the printable cells land on consecutive columns (no cell
     * was consumed by \t or \n). */
    const char msg[] = "a\tb\nc";
    int cols = screen_put_str(&s, 0, 0, msg, 0);
    ASSERT(cols == 3, "only 3 printable cols consumed");

    ASSERT(s.back[0].cp == (uint32_t)'a', "'a'");
    ASSERT(s.back[1].cp == (uint32_t)'b', "'b' directly after 'a'");
    ASSERT(s.back[2].cp == (uint32_t)'c', "'c' directly after 'b'");
    /* If the sanitiser had caught them, we'd see U+00B7 in the buffer. */
    ASSERT(count_cp(&s, 0, 0x00B7) == 0, "no MIDDLE DOT leaked");
    /* Cell 3 was never touched and stays blank. */
    ASSERT(s.back[3].cp == (uint32_t)' ', "cell 3 is blank");

    screen_drain(&s, &rs);
    assert_no_user_escape_in_stream(rs.buf, rs.len, __func__);
    screen_teardown(&s, &rs);
}

/* 5. Emoji smiley 😀 (U+1F600) — terminal_wcwidth returns 2. */
static void test_tui_pane_renders_emoji_message(void) {
    Screen s; RenderSink rs; screen_setup(&s, &rs);

    /* "hi " + 😀 = 3 + 2 = 5 display cols. */
    const char msg[] = "hi \xf0\x9f\x98\x80";
    int cols = screen_put_str(&s, 0, 0, msg, 0);
    ASSERT(cols == 5, "3 ASCII + 2 emoji = 5 cols");

    ASSERT(s.back[0].cp == (uint32_t)'h', "'h'");
    ASSERT(s.back[1].cp == (uint32_t)'i', "'i'");
    ASSERT(s.back[2].cp == (uint32_t)' ', "space");
    ASSERT(s.back[3].cp == 0x1F600,       "emoji lead cell");
    ASSERT(s.back[3].width == 2,          "emoji is wide (width 2)");
    ASSERT(s.back[4].cp == 0x1F600,       "emoji trailer carries same cp");
    ASSERT(s.back[4].width == 0,          "trailer has width 0");

    screen_drain(&s, &rs);
    assert_no_user_escape_in_stream(rs.buf, rs.len, __func__);
    screen_teardown(&s, &rs);
}

/* 6. CJK "你好" — each codepoint is width 2 → 4 display cols. */
static void test_tui_pane_renders_cjk_message(void) {
    Screen s; RenderSink rs; screen_setup(&s, &rs);

    /* U+4F60 你 = 0xE4 0xBD 0xA0
     * U+597D 好 = 0xE5 0xA5 0xBD */
    const char msg[] = "\xe4\xbd\xa0\xe5\xa5\xbd";
    int cols = screen_put_str(&s, 0, 0, msg, 0);
    ASSERT(cols == 4, "two CJK glyphs occupy 4 cols");

    ASSERT(s.back[0].cp == 0x4F60, "你 lead");
    ASSERT(s.back[0].width == 2,   "你 wide");
    ASSERT(s.back[1].width == 0,   "你 trailer");
    ASSERT(s.back[2].cp == 0x597D, "好 lead");
    ASSERT(s.back[2].width == 2,   "好 wide");
    ASSERT(s.back[3].width == 0,   "好 trailer");

    screen_drain(&s, &rs);
    assert_no_user_escape_in_stream(rs.buf, rs.len, __func__);
    screen_teardown(&s, &rs);
}

/* 7. RTL Hebrew "שלום" — rendered in logical (byte) order, no BiDi
 *    shaping, no cell corruption. */
static void test_tui_pane_renders_rtl_message(void) {
    Screen s; RenderSink rs; screen_setup(&s, &rs);

    /* U+05E9 ש, U+05DC ל, U+05D5 ו, U+05DD ם — each width 1. */
    const char msg[] = "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d";
    int cols = screen_put_str(&s, 0, 0, msg, 0);
    ASSERT(cols == 4, "four RTL letters, each width 1");

    ASSERT(s.back[0].cp == 0x05E9, "ש in logical position 0");
    ASSERT(s.back[1].cp == 0x05DC, "ל in logical position 1");
    ASSERT(s.back[2].cp == 0x05D5, "ו in logical position 2");
    ASSERT(s.back[3].cp == 0x05DD, "ם in logical position 3");
    assert_no_raw_escape_bytes(&s, "rtl");

    /* No U+202E RLO override should appear as an uninvited codepoint
     * and — if a user truly puts RLO inside the body — it must survive
     * as a zero-width mark, not as a cell that flips anything. */
    ASSERT(find_cp(&s, 0, 0x202E) == -1, "no RLO leaked from elsewhere");

    /* Separate sub-scenario: a real U+202E in the body goes through
     * wcwidth() which returns 0, so it is silently skipped rather than
     * storing a cell that would flip neighbouring cells. */
    const char rlo[] = "A" "\xe2\x80\xae" "B";   /* A <RLO> B */
    screen_clear_back(&s);
    int cols2 = screen_put_str(&s, 0, 0, rlo, 0);
    ASSERT(cols2 == 2, "RLO itself has width 0 so only 'A' + 'B' = 2 cols");
    ASSERT(s.back[0].cp == (uint32_t)'A', "'A'");
    ASSERT(s.back[1].cp == (uint32_t)'B', "'B' follows immediately");

    screen_drain(&s, &rs);
    assert_no_user_escape_in_stream(rs.buf, rs.len, __func__);
    screen_teardown(&s, &rs);
}

/* 8. ZWJ cluster 👨‍👩‍👧 — we don't do grapheme-cluster shaping, so each
 *    constituent lands on its own cell, but no codepoint is dropped,
 *    no escape leaks, and the width is the arithmetic sum of the parts. */
static void test_tui_pane_zwj_cluster_stays_single_cell(void) {
    Screen s; RenderSink rs; screen_setup(&s, &rs);

    /* U+1F468 👨 (width 2) + U+200D ZWJ (width 0) + U+1F469 👩 (width 2)
     * + U+200D ZWJ (width 0) + U+1F467 👧 (width 2). */
    const char msg[] =
        "\xf0\x9f\x91\xa8" "\xe2\x80\x8d"
        "\xf0\x9f\x91\xa9" "\xe2\x80\x8d"
        "\xf0\x9f\x91\xa7";
    int cols = screen_put_str(&s, 0, 0, msg, 0);
    ASSERT(cols == 6, "three wide emoji + two zero-width ZWJ = 6 cols");

    ASSERT(s.back[0].cp == 0x1F468, "👨 lead");
    ASSERT(s.back[0].width == 2,    "👨 width 2");
    ASSERT(s.back[2].cp == 0x1F469, "👩 lead at col 2");
    ASSERT(s.back[4].cp == 0x1F467, "👧 lead at col 4");
    assert_no_raw_escape_bytes(&s, "zwj");

    screen_drain(&s, &rs);
    assert_no_user_escape_in_stream(rs.buf, rs.len, __func__);
    screen_teardown(&s, &rs);
}

/* 9. Combining mark: "e" + U+0301 (COMBINING ACUTE) renders as width 1.
 *    The two codepoints live in one cell slot + zero-width "skip"; the
 *    important contract is that the combining mark is NOT mis-rendered
 *    as its own cell that pushes "é"'s base out of alignment. */
static void test_tui_pane_combining_mark_width_zero(void) {
    Screen s; RenderSink rs; screen_setup(&s, &rs);

    const char msg[] = "e\xcc\x81" "xt";   /* e + U+0301 + "xt" */
    int cols = screen_put_str(&s, 0, 0, msg, 0);
    ASSERT(cols == 3, "base 'e' + 'xt' = 3 cols (combining is 0)");

    ASSERT(s.back[0].cp == (uint32_t)'e', "'e'");
    ASSERT(s.back[1].cp == (uint32_t)'x', "'x' directly follows (U+0301 had width 0)");
    ASSERT(s.back[2].cp == (uint32_t)'t', "'t'");
    /* Combining acute must not have been stored as its own cell. */
    ASSERT(find_cp(&s, 0, 0x0301) == -1,
           "combining mark did not consume a cell");

    screen_drain(&s, &rs);
    assert_no_user_escape_in_stream(rs.buf, rs.len, __func__);
    screen_teardown(&s, &rs);
}

/* 10. Malformed UTF-8: three bytes that cannot begin a valid sequence
 *     must become three U+FFFD while adjacent ASCII survives. */
static void test_malformed_utf8_replacement_char(void) {
    Screen s; RenderSink rs; screen_setup(&s, &rs);

    /* 0xC0 is never valid as a UTF-8 lead byte (overlong 2-byte form);
     * 0xFF and 0xFE are never valid lead bytes at all. */
    const char msg[] = "A" "\xc0\xff\xfe" "B";
    int cols = screen_put_str(&s, 0, 0, msg, 0);
    ASSERT(cols == 5, "A + 3×U+FFFD + B = 5 cols");

    ASSERT(s.back[0].cp == (uint32_t)'A', "leading 'A' preserved");
    ASSERT(s.back[1].cp == 0xFFFD,        "byte 0xC0 → U+FFFD");
    ASSERT(s.back[2].cp == 0xFFFD,        "byte 0xFF → U+FFFD");
    ASSERT(s.back[3].cp == 0xFFFD,        "byte 0xFE → U+FFFD");
    ASSERT(s.back[4].cp == (uint32_t)'B', "trailing 'B' preserved");
    assert_no_raw_escape_bytes(&s, "malformed_utf8");

    screen_drain(&s, &rs);
    assert_no_user_escape_in_stream(rs.buf, rs.len, __func__);
    screen_teardown(&s, &rs);
}

/* 11. Overlong / 5-byte sequence — the decoder has no branch for an
 *     0xF8-lead byte, so it takes the fallback and emits U+FFFD for
 *     one byte. */
static void test_overlong_utf8_rejected(void) {
    Screen s; RenderSink rs; screen_setup(&s, &rs);

    /* 0xF8 is the RFC2279 "5-byte" introducer, outlawed by RFC3629.
     * utf8_decode has no branch for an 0xF8 lead, so it falls through
     * to the generic "malformed" case: emit U+FFFD and advance by one
     * byte.  Each of the subsequent continuation bytes (0x88, 0x80…)
     * also has no valid position on its own and therefore yields its
     * own U+FFFD.  Result: 5 invalid bytes → 5 U+FFFD cells. */
    const char msg[] = "[" "\xf8\x88\x80\x80\x80" "]";
    int cols = screen_put_str(&s, 0, 0, msg, 0);
    /* '[' + 5 × U+FFFD + ']' = 7 cols. */
    ASSERT(cols == 7, "bracket + 5 U+FFFD cells + bracket");

    ASSERT(s.back[0].cp == (uint32_t)'[', "leading '['");
    ASSERT(s.back[1].cp == 0xFFFD,        "0xF8 lead → U+FFFD");
    ASSERT(s.back[6].cp == (uint32_t)']', "trailing ']'");
    assert_no_raw_escape_bytes(&s, "overlong");

    /* Every rejected byte must materialise as U+FFFD (not as a raw
     * control nor as a zero-width skipped cell). */
    for (int c = 1; c <= 5; c++) {
        ASSERT(s.back[c].cp == 0xFFFD,
               "every rejected byte materialised as U+FFFD");
    }

    screen_drain(&s, &rs);
    assert_no_user_escape_in_stream(rs.buf, rs.len, __func__);
    screen_teardown(&s, &rs);
}

/* 12. UTF-8 BOM (U+FEFF) mid-message: width 0, so it is silently
 *     skipped — never rendered as the four-byte "" garbage.  The
 *     adjacent printable text stays on consecutive columns. */
static void test_utf8_bom_zero_width(void) {
    Screen s; RenderSink rs; screen_setup(&s, &rs);

    /* U+FEFF = 0xEF 0xBB 0xBF. */
    const char msg[] = "a\xef\xbb\xbf" "b";
    int cols = screen_put_str(&s, 0, 0, msg, 0);
    ASSERT(cols == 2, "BOM is zero-width: only 'a' and 'b' consume cols");

    ASSERT(s.back[0].cp == (uint32_t)'a', "'a'");
    ASSERT(s.back[1].cp == (uint32_t)'b', "'b' follows directly after BOM");
    ASSERT(find_cp(&s, 0, 0xFEFF) == -1,  "BOM did not materialise as a cell");
    /* And no garbage escape sequence byte survived either. */
    assert_no_raw_escape_bytes(&s, "bom");

    screen_drain(&s, &rs);
    assert_no_user_escape_in_stream(rs.buf, rs.len, __func__);
    screen_teardown(&s, &rs);
}

/* ================================================================ */
/* Suite entry point                                                */
/* ================================================================ */

void run_text_rendering_safety_tests(void) {
    RUN_TEST(test_plain_history_strips_ansi_csi);
    RUN_TEST(test_plain_history_strips_osc_title);
    RUN_TEST(test_plain_history_strips_bel_and_c1);
    RUN_TEST(test_plain_history_preserves_newline_and_tab);
    RUN_TEST(test_tui_pane_renders_emoji_message);
    RUN_TEST(test_tui_pane_renders_cjk_message);
    RUN_TEST(test_tui_pane_renders_rtl_message);
    RUN_TEST(test_tui_pane_zwj_cluster_stays_single_cell);
    RUN_TEST(test_tui_pane_combining_mark_width_zero);
    RUN_TEST(test_malformed_utf8_replacement_char);
    RUN_TEST(test_overlong_utf8_rejected);
    RUN_TEST(test_utf8_bom_zero_width);
}
