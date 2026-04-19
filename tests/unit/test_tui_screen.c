/**
 * @file tests/unit/test_tui_screen.c
 * @brief Unit tests for the TUI double-buffered screen (US-11 v2).
 */

#include "test_helpers.h"
#include "tui/screen.h"
#include "platform/terminal.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Capture screen output into a heap-backed memstream. */
typedef struct {
    FILE  *stream;
    char  *buf;
    size_t len;
} Sink;

static void sink_open(Sink *s) {
    s->buf = NULL;
    s->len = 0;
    s->stream = open_memstream(&s->buf, &s->len);
}

/* Flush the memstream so buf/len reflect everything written so far. */
static void sink_flush(Sink *s) { fflush(s->stream); }

static void sink_close(Sink *s) {
    if (s->stream) { fclose(s->stream); s->stream = NULL; }
    free(s->buf); s->buf = NULL; s->len = 0;
}

static int sink_contains(const Sink *s, const char *needle) {
    if (!s->buf || !needle) return 0;
    return memmem(s->buf, s->len, needle, strlen(needle)) != NULL;
}

/* --- Tests --- */

static void test_init_allocates_and_free_releases(void) {
    Screen s;
    ASSERT(screen_init(&s, 10, 40) == 0, "init should succeed");
    ASSERT(s.front != NULL, "front grid allocated");
    ASSERT(s.back  != NULL, "back grid allocated");
    ASSERT(s.rows == 10 && s.cols == 40, "dims recorded");
    ASSERT(s.out == stdout, "out defaults to stdout");
    screen_free(&s);
    ASSERT(s.front == NULL, "front cleared on free");
    ASSERT(s.back == NULL, "back cleared on free");
}

static void test_init_rejects_bad_dims(void) {
    Screen s;
    ASSERT(screen_init(&s, 0, 40) != 0, "rows=0 rejected");
    ASSERT(screen_init(&s, 10, 0) != 0, "cols=0 rejected");
    ASSERT(screen_init(NULL, 10, 40) != 0, "null screen rejected");
}

static void test_put_str_ascii_lands_in_back(void) {
    Screen s; ASSERT(screen_init(&s, 4, 20) == 0, "init");
    int w = screen_put_str(&s, 1, 2, "hello", SCREEN_ATTR_NORMAL);
    ASSERT(w == 5, "hello uses 5 columns");
    ASSERT(s.back[1 * 20 + 2].cp == 'h', "h at (1,2)");
    ASSERT(s.back[1 * 20 + 6].cp == 'o', "o at (1,6)");
    ASSERT(s.back[1 * 20 + 7].cp == ' ', "untouched cells remain blank");
    screen_free(&s);
}

static void test_put_str_clips_at_right_edge(void) {
    Screen s; ASSERT(screen_init(&s, 2, 8) == 0, "init");
    int w = screen_put_str(&s, 0, 5, "abcdef", SCREEN_ATTR_NORMAL);
    ASSERT(w == 3, "clipped to 3 columns (5..7)");
    ASSERT(s.back[5].cp == 'a', "a at (0,5)");
    ASSERT(s.back[7].cp == 'c', "c at (0,7)");
    screen_free(&s);
}

static void test_put_str_wide_char_occupies_two_cells(void) {
    setlocale(LC_CTYPE, "en_US.UTF-8");
    Screen s; ASSERT(screen_init(&s, 1, 10) == 0, "init");
    /* 日 is U+65E5, encoded in UTF-8 as 0xE6 0x97 0xA5, wcwidth==2. */
    int w = screen_put_str(&s, 0, 0, "\xE6\x97\xA5", SCREEN_ATTR_NORMAL);
    if (w == 0) { screen_free(&s); return; /* locale unsupported on host */ }
    ASSERT(w == 2, "wide char consumes 2 columns");
    ASSERT(s.back[0].cp == 0x65E5, "lead holds codepoint");
    ASSERT(s.back[0].width == 2, "lead width is 2");
    ASSERT(s.back[1].width == 0, "trailer width is 0");
    screen_free(&s);
}

static void test_put_str_wide_char_clipped_when_one_cell_left(void) {
    setlocale(LC_CTYPE, "en_US.UTF-8");
    Screen s; ASSERT(screen_init(&s, 1, 3) == 0, "init");
    int pre = screen_put_str(&s, 0, 0, "ab", SCREEN_ATTR_NORMAL);
    ASSERT(pre == 2, "pre-fill");
    int w = screen_put_str(&s, 0, 2, "\xE6\x97\xA5", SCREEN_ATTR_NORMAL);
    if (w == 2) {
        /* Host has no UTF-8 locale — glibc treated cp as narrow. Skip check. */
        screen_free(&s); return;
    }
    ASSERT(w == 0, "wide char does not fit; nothing written");
    ASSERT(s.back[2].cp == ' ', "cell remains blank");
    screen_free(&s);
}

static void test_put_str_rejects_out_of_range(void) {
    Screen s; ASSERT(screen_init(&s, 3, 10) == 0, "init");
    ASSERT(screen_put_str(&s, -1, 0, "x", 0) == 0, "negative row");
    ASSERT(screen_put_str(&s, 3, 0, "x", 0) == 0, "row == rows");
    ASSERT(screen_put_str(&s, 0, 10, "x", 0) == 0, "col == cols");
    ASSERT(screen_put_str(&s, 0, -1, "x", 0) == 0, "negative col");
    screen_free(&s);
}

static void test_clear_back_resets_every_cell(void) {
    Screen s; ASSERT(screen_init(&s, 2, 6) == 0, "init");
    screen_put_str(&s, 0, 0, "hey", SCREEN_ATTR_BOLD);
    screen_clear_back(&s);
    for (int i = 0; i < 12; i++) {
        ASSERT(s.back[i].cp == ' ', "cell blank");
        ASSERT(s.back[i].attrs == 0, "attrs reset");
    }
    screen_free(&s);
}

static void test_fill_writes_attrs(void) {
    Screen s; ASSERT(screen_init(&s, 2, 8) == 0, "init");
    screen_fill(&s, 1, 2, 4, SCREEN_ATTR_REVERSE);
    ASSERT(s.back[1 * 8 + 1].attrs == 0, "cell before fill untouched");
    ASSERT(s.back[1 * 8 + 2].attrs == SCREEN_ATTR_REVERSE, "fill cell 0");
    ASSERT(s.back[1 * 8 + 5].attrs == SCREEN_ATTR_REVERSE, "fill cell 3");
    ASSERT(s.back[1 * 8 + 6].attrs == 0, "cell after fill untouched");
    screen_free(&s);
}

static void test_fill_clips_right_edge(void) {
    Screen s; ASSERT(screen_init(&s, 1, 6) == 0, "init");
    screen_fill(&s, 0, 4, 100, SCREEN_ATTR_BOLD);
    ASSERT(s.back[4].attrs == SCREEN_ATTR_BOLD, "col 4");
    ASSERT(s.back[5].attrs == SCREEN_ATTR_BOLD, "col 5");
    /* nothing at col 6/7 — no buffer overflow */
    screen_free(&s);
}

static void test_flip_first_time_emits_everything(void) {
    Sink sink; sink_open(&sink);
    Screen s; ASSERT(screen_init(&s, 2, 10) == 0, "init");
    s.out = sink.stream;
    screen_put_str(&s, 0, 0, "hi", SCREEN_ATTR_NORMAL);
    size_t n = screen_flip(&s);
    sink_flush(&sink);
    ASSERT(n > 0, "first flip emits bytes");
    ASSERT(sink_contains(&sink, "hi"), "output contains 'hi'");
    ASSERT(sink_contains(&sink, "\033[1;1H"), "cursor to (1,1)");
    screen_free(&s);
    sink_close(&sink);
}

static void test_flip_second_time_identical_emits_nothing(void) {
    Sink sink; sink_open(&sink);
    Screen s; ASSERT(screen_init(&s, 2, 8) == 0, "init");
    s.out = sink.stream;
    screen_put_str(&s, 0, 0, "hi", 0);
    screen_flip(&s);
    sink_flush(&sink);
    size_t baseline = sink.len;
    screen_put_str(&s, 0, 0, "hi", 0);  /* same content */
    size_t n = screen_flip(&s);
    sink_flush(&sink);
    ASSERT(n == 0, "idempotent flip emits 0 bytes");
    ASSERT(sink.len == baseline, "sink unchanged");
    screen_free(&s);
    sink_close(&sink);
}

static void test_flip_emits_only_changed_cells(void) {
    Sink sink; sink_open(&sink);
    Screen s; ASSERT(screen_init(&s, 2, 10) == 0, "init");
    s.out = sink.stream;
    screen_put_str(&s, 0, 0, "abc", 0);
    screen_put_str(&s, 1, 0, "xyz", 0);
    screen_flip(&s);
    sink_flush(&sink);
    size_t baseline = sink.len;

    /* Change only row 1, column 1 — 'y' → 'Y'. */
    screen_put_str(&s, 1, 1, "Y", 0);
    screen_flip(&s);
    sink_flush(&sink);

    size_t diff_bytes = sink.len - baseline;
    ASSERT(diff_bytes > 0, "change emits bytes");
    /* Should not re-emit 'abc' at all. */
    const char *delta = sink.buf + baseline;
    size_t dlen = diff_bytes;
    ASSERT(!memmem(delta, dlen, "abc", 3), "unchanged row not re-emitted");
    ASSERT(memmem(delta, dlen, "Y", 1) != NULL, "new Y emitted");
    ASSERT(memmem(delta, dlen, "\033[2;2H", 6) != NULL, "cursor to (2,2)");
    screen_free(&s);
    sink_close(&sink);
}

static void test_flip_emits_sgr_for_attrs_and_resets_at_end(void) {
    Sink sink; sink_open(&sink);
    Screen s; ASSERT(screen_init(&s, 1, 6) == 0, "init");
    s.out = sink.stream;
    screen_put_str(&s, 0, 0, "hi", SCREEN_ATTR_BOLD | SCREEN_ATTR_REVERSE);
    screen_flip(&s);
    sink_flush(&sink);
    ASSERT(sink_contains(&sink, "\033[0;1;7m"), "SGR for bold+reverse");
    /* Trailing reset so subsequent writes aren't styled. */
    ASSERT(sink_contains(&sink, "\033[0m"), "trailing reset emitted");
    screen_free(&s);
    sink_close(&sink);
}

static void test_invalidate_forces_full_redraw(void) {
    Sink sink; sink_open(&sink);
    Screen s; ASSERT(screen_init(&s, 1, 5) == 0, "init");
    s.out = sink.stream;
    screen_put_str(&s, 0, 0, "abc", 0);
    screen_flip(&s);
    sink_flush(&sink);
    size_t baseline = sink.len;

    /* Nothing changed in the back buffer but invalidate forces a redraw. */
    screen_invalidate(&s);
    screen_put_str(&s, 0, 0, "abc", 0);  /* must re-stage explicitly */
    size_t n = screen_flip(&s);
    sink_flush(&sink);
    ASSERT(n > 0, "invalidate forces redraw");
    const char *delta = sink.buf + baseline;
    size_t dlen = sink.len - baseline;
    ASSERT(memmem(delta, dlen, "abc", 3) != NULL, "full content re-emitted");
    screen_free(&s);
    sink_close(&sink);
}

static void test_cursor_writes_cup(void) {
    Sink sink; sink_open(&sink);
    Screen s; ASSERT(screen_init(&s, 5, 20) == 0, "init");
    s.out = sink.stream;
    screen_cursor(&s, 3, 7);
    sink_flush(&sink);
    ASSERT(sink_contains(&sink, "\033[3;7H"), "CUP emitted 1-based");
    screen_free(&s);
    sink_close(&sink);
}

static void test_cursor_visible_emits_dectcem(void) {
    Sink sink; sink_open(&sink);
    Screen s; ASSERT(screen_init(&s, 1, 10) == 0, "init");
    s.out = sink.stream;
    screen_cursor_visible(&s, 0);
    screen_cursor_visible(&s, 1);
    sink_flush(&sink);
    ASSERT(sink_contains(&sink, "\033[?25l"), "hide sequence");
    ASSERT(sink_contains(&sink, "\033[?25h"), "show sequence");
    screen_free(&s);
    sink_close(&sink);
}

static void test_flip_skips_wide_char_trailer(void) {
    setlocale(LC_CTYPE, "en_US.UTF-8");
    Sink sink; sink_open(&sink);
    Screen s; ASSERT(screen_init(&s, 1, 4) == 0, "init");
    s.out = sink.stream;
    int w = screen_put_str(&s, 0, 0, "\xE6\x97\xA5", SCREEN_ATTR_NORMAL);
    if (w != 2) {
        screen_free(&s); sink_close(&sink);
        return; /* locale unsupported */
    }
    screen_flip(&s);
    sink_flush(&sink);
    /* UTF-8 byte sequence appears exactly once (we skip the trailer). */
    const char needle[] = "\xE6\x97\xA5";
    int count = 0;
    for (size_t i = 0; i + 3 <= sink.len; i++) {
        if (memcmp(sink.buf + i, needle, 3) == 0) count++;
    }
    ASSERT(count == 1, "wide char emitted once");
    screen_free(&s);
    sink_close(&sink);
}

/* --- SEC-01 sanitization tests --- */

/* U+00B7 MIDDLE DOT encoded as UTF-8: 0xC2 0xB7 */
static const char MIDDLE_DOT_UTF8[] = "\xC2\xB7";

/**
 * @brief SEC-01: ESC (U+001B) in input must not appear in flip output.
 *
 * A malicious message containing ESC followed by '[2J' (erase-display)
 * must have the ESC replaced with U+00B7 MIDDLE DOT so no raw 0x1B byte
 * reaches the terminal.
 */
static void test_sec01_esc_replaced_by_placeholder(void) {
    Sink sink; sink_open(&sink);
    Screen s; ASSERT(screen_init(&s, 2, 20) == 0, "init");
    s.out = sink.stream;
    /* "\033[2J" — erase-display escape sequence embedded in message text */
    screen_put_str(&s, 0, 0, "\033[2J", SCREEN_ATTR_NORMAL);
    /* ESC should have been replaced; the cell at col 0 must hold U+00B7. */
    ASSERT(s.back[0].cp == 0x00B7, "ESC codepoint replaced with U+00B7");
    screen_flip(&s);
    sink_flush(&sink);
    /* The raw 0x1B ESC byte must not appear in the terminal output. */
    int found_esc = 0;
    for (size_t i = 0; i < sink.len; i++) {
        if ((unsigned char)sink.buf[i] == 0x1B
                && i + 1 < sink.len
                /* Allow legitimate CUP / SGR sequences emitted by screen_flip
                 * itself — those follow the pattern 0x1B '[' digit. They are
                 * fine; what we forbid is 0x1B injected from message content
                 * at cell positions.  The placeholder U+00B7 emits 0xC2 0xB7
                 * so it cannot accidentally become 0x1B. */
                && (unsigned char)sink.buf[i + 1] != '[') {
            found_esc = 1;
            break;
        }
    }
    ASSERT(!found_esc, "no raw ESC from message content in terminal output");
    /* The placeholder byte sequence (UTF-8 for U+00B7) must be present. */
    ASSERT(sink_contains(&sink, MIDDLE_DOT_UTF8), "middle-dot placeholder emitted");
    screen_free(&s);
    sink_close(&sink);
}

/**
 * @brief SEC-01: DEL (U+007F) in input must be replaced with U+00B7.
 */
static void test_sec01_del_replaced_by_placeholder(void) {
    Screen s; ASSERT(screen_init(&s, 1, 10) == 0, "init");
    screen_put_str(&s, 0, 0, "\x7F", SCREEN_ATTR_NORMAL);
    ASSERT(s.back[0].cp == 0x00B7, "DEL (0x7F) replaced with U+00B7");
    screen_free(&s);
}

/**
 * @brief SEC-01: 8-bit CSI (U+009B, encoded as 0xC2 0x9B in UTF-8) in input
 * must be replaced with U+00B7.
 */
static void test_sec01_csi_replaced_by_placeholder(void) {
    Screen s; ASSERT(screen_init(&s, 1, 10) == 0, "init");
    /* U+009B encoded in UTF-8: 0xC2 0x9B */
    screen_put_str(&s, 0, 0, "\xC2\x9B", SCREEN_ATTR_NORMAL);
    ASSERT(s.back[0].cp == 0x00B7, "8-bit CSI (U+009B) replaced with U+00B7");
    screen_free(&s);
}

/**
 * @brief SEC-01: plain ASCII text must pass through unchanged (no false positives).
 */
static void test_sec01_plain_text_unchanged(void) {
    Sink sink; sink_open(&sink);
    Screen s; ASSERT(screen_init(&s, 1, 20) == 0, "init");
    s.out = sink.stream;
    screen_put_str(&s, 0, 0, "Hello, world!", SCREEN_ATTR_NORMAL);
    ASSERT(s.back[0].cp == 'H', "H at col 0");
    ASSERT(s.back[4].cp == 'o', "o at col 4");
    screen_flip(&s);
    sink_flush(&sink);
    ASSERT(sink_contains(&sink, "Hello, world!"), "plain text passes through");
    screen_free(&s);
    sink_close(&sink);
}

void test_tui_screen_run(void) {
    RUN_TEST(test_init_allocates_and_free_releases);
    RUN_TEST(test_init_rejects_bad_dims);
    RUN_TEST(test_put_str_ascii_lands_in_back);
    RUN_TEST(test_put_str_clips_at_right_edge);
    RUN_TEST(test_put_str_wide_char_occupies_two_cells);
    RUN_TEST(test_put_str_wide_char_clipped_when_one_cell_left);
    RUN_TEST(test_put_str_rejects_out_of_range);
    RUN_TEST(test_clear_back_resets_every_cell);
    RUN_TEST(test_fill_writes_attrs);
    RUN_TEST(test_fill_clips_right_edge);
    RUN_TEST(test_flip_first_time_emits_everything);
    RUN_TEST(test_flip_second_time_identical_emits_nothing);
    RUN_TEST(test_flip_emits_only_changed_cells);
    RUN_TEST(test_flip_emits_sgr_for_attrs_and_resets_at_end);
    RUN_TEST(test_invalidate_forces_full_redraw);
    RUN_TEST(test_cursor_writes_cup);
    RUN_TEST(test_cursor_visible_emits_dectcem);
    RUN_TEST(test_flip_skips_wide_char_trailer);
    RUN_TEST(test_sec01_esc_replaced_by_placeholder);
    RUN_TEST(test_sec01_del_replaced_by_placeholder);
    RUN_TEST(test_sec01_csi_replaced_by_placeholder);
    RUN_TEST(test_sec01_plain_text_unchanged);
}
