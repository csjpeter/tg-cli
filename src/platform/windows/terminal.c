/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * Windows terminal implementation for MinGW-w64.
 * Uses Windows Console API: GetConsoleMode/SetConsoleMode,
 * GetConsoleScreenBufferInfo, ReadConsoleInput, WaitForSingleObject.
 */

#include "../terminal.h"
#include "core/wcwidth.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct TermRawState {
    DWORD saved_in_mode;
    DWORD saved_out_mode;
    int   active;
};

/* ---- Resize flag (set via ReadConsoleInput WINDOW_BUFFER_SIZE_EVENT) ---- */
static volatile int g_resize_pending = 0;

/* ---- Cleanup handler state ---- */
static TermRawState *g_cleanup_state = NULL;

static BOOL WINAPI ctrl_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT
        || type == CTRL_CLOSE_EVENT
        || type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT) {
        if (g_cleanup_state && g_cleanup_state->active) {
            HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
            HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
            SetConsoleMode(hin,  g_cleanup_state->saved_in_mode);
            SetConsoleMode(hout, g_cleanup_state->saved_out_mode);
            /* Show cursor */
            CONSOLE_CURSOR_INFO ci = { 100, TRUE };
            SetConsoleCursorInfo(hout, &ci);
        }
        /* Let the default handler run (terminates the process). */
        return FALSE;
    }
    return FALSE;
}

/* ---- terminal_cols / terminal_rows ---- */

int terminal_cols(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return (int)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
    return 80;
}

int terminal_rows(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return (int)(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
    return 0;
}

/* ---- terminal_is_tty ---- */

int terminal_is_tty(int fd) {
    HANDLE h;
    if (fd == 0) h = GetStdHandle(STD_INPUT_HANDLE);
    else if (fd == 1) h = GetStdHandle(STD_OUTPUT_HANDLE);
    else if (fd == 2) h = GetStdHandle(STD_ERROR_HANDLE);
    else return 0;
    DWORD mode;
    return GetConsoleMode(h, &mode) ? 1 : 0;
}

/* ---- Raw mode ---- */

TermRawState *terminal_raw_enter(void) {
    HANDLE hin  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    TermRawState *state = (TermRawState *)malloc(sizeof(TermRawState));
    if (!state) return NULL;

    if (!GetConsoleMode(hin,  &state->saved_in_mode) ||
        !GetConsoleMode(hout, &state->saved_out_mode)) {
        free(state);
        return NULL;
    }

    /* Disable line input, echo, and Ctrl-C processing (we handle it). */
    DWORD new_in = state->saved_in_mode
                   & ~(DWORD)(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT
                              | ENABLE_PROCESSED_INPUT);
    /* Enable window input events so we can detect resize. */
    new_in |= ENABLE_WINDOW_INPUT;
    SetConsoleMode(hin, new_in);

    /* Enable VT processing for ANSI escape output if available (Win10+). */
    DWORD new_out = state->saved_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hout, new_out);

    state->active = 1;
    return state;
}

void terminal_raw_exit(TermRawState **state) {
    if (!state || !*state) return;
    TermRawState *s = *state;
    if (s->active) {
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE),  s->saved_in_mode);
        SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), s->saved_out_mode);
    }
    free(s);
    *state = NULL;
}

/* ---- Key reading ---- */

static int g_last_printable = 0;

int terminal_last_printable(void) { return g_last_printable; }

TermKey terminal_read_key(void) {
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    g_last_printable = 0;

    for (;;) {
        INPUT_RECORD ir;
        DWORD n = 0;
        if (!ReadConsoleInput(hin, &ir, 1, &n) || n == 0)
            return TERM_KEY_IGNORE;

        if (ir.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            g_resize_pending = 1;
            continue;
        }

        if (ir.EventType != KEY_EVENT) continue;
        if (!ir.Event.KeyEvent.bKeyDown) continue;

        WORD vk  = ir.Event.KeyEvent.wVirtualKeyCode;
        DWORD ctrl = ir.Event.KeyEvent.dwControlKeyState;
        BOOL ctrl_pressed = (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
        WCHAR ch = ir.Event.KeyEvent.uChar.UnicodeChar;

        /* Virtual key dispatch first. */
        switch (vk) {
        case VK_UP:     return TERM_KEY_PREV_LINE;
        case VK_DOWN:   return TERM_KEY_NEXT_LINE;
        case VK_LEFT:   return TERM_KEY_LEFT;
        case VK_RIGHT:  return TERM_KEY_RIGHT;
        case VK_PRIOR:  return TERM_KEY_PREV_PAGE;
        case VK_NEXT:   return TERM_KEY_NEXT_PAGE;
        case VK_HOME:   return TERM_KEY_HOME;
        case VK_END:    return TERM_KEY_END;
        case VK_DELETE: return TERM_KEY_DELETE;
        case VK_ESCAPE: return TERM_KEY_ESC;
        case VK_RETURN: return TERM_KEY_ENTER;
        case VK_BACK:   return TERM_KEY_BACK;
        default: break;
        }

        /* Ctrl+letter combinations. */
        if (ctrl_pressed) {
            switch (vk) {
            case 'C': return TERM_KEY_QUIT;
            case 'A': return TERM_KEY_CTRL_A;
            case 'E': return TERM_KEY_CTRL_E;
            case 'K': return TERM_KEY_CTRL_K;
            case 'W': return TERM_KEY_CTRL_W;
            case 'D': return TERM_KEY_CTRL_D;
            default:  return TERM_KEY_IGNORE;
            }
        }

        /* Printable character. */
        if (ch >= 32 && ch <= 126) {
            g_last_printable = (int)ch;
            return TERM_KEY_IGNORE;
        }
        if (ch == '\r' || ch == '\n') return TERM_KEY_ENTER;
        if (ch == 0x1b) return TERM_KEY_ESC;
        if (ch == '\b' || ch == 127) return TERM_KEY_BACK;

        return TERM_KEY_IGNORE;
    }
}

/* ---- Wait for key ---- */

int terminal_wait_key(int timeout_ms) {
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD ms = (timeout_ms < 0) ? INFINITE : (DWORD)timeout_ms;
    DWORD rc = WaitForSingleObject(hin, ms);
    if (rc == WAIT_OBJECT_0) {
        /* Check if it's a real key event (not just a mouse or resize). */
        DWORD n = 0;
        if (GetNumberOfConsoleInputEvents(hin, &n) && n > 0)
            return 1;
        return 0;
    }
    if (rc == WAIT_TIMEOUT) return 0;
    return -1;
}

/* ---- wcwidth ---- */

int terminal_wcwidth(uint32_t cp) { return core_wcwidth(cp); }

/* ---- Password input ---- */

int terminal_read_password(const char *prompt, char *buf, size_t size) {
    if (!buf || size == 0) return -1;
    HANDLE hin  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);

    if (prompt) {
        DWORD written;
        WriteConsoleA(hout, prompt, (DWORD)strlen(prompt), &written, NULL);
        WriteConsoleA(hout, ": ", 2, &written, NULL);
    }

    DWORD saved_mode;
    GetConsoleMode(hin, &saved_mode);
    SetConsoleMode(hin, saved_mode & ~(DWORD)ENABLE_ECHO_INPUT
                        | ENABLE_LINE_INPUT);

    /* Read characters one at a time to avoid \r\n issues. */
    size_t pos = 0;
    for (;;) {
        DWORD n = 0;
        char c = 0;
        if (!ReadConsoleA(hin, &c, 1, &n, NULL) || n == 0) break;
        if (c == '\r' || c == '\n') break;
        if ((c == '\b' || c == 127) && pos > 0) { pos--; continue; }
        if (pos < size - 1) buf[pos++] = c;
    }
    buf[pos] = '\0';

    SetConsoleMode(hin, saved_mode);

    DWORD written;
    WriteConsoleA(hout, "\r\n", 2, &written, NULL);
    return (int)pos;
}

/* ---- Resize notifications ---- */

void terminal_enable_resize_notifications(void) {
    /* On Windows, resize events arrive via ReadConsoleInput
     * (WINDOW_BUFFER_SIZE_EVENT) — no extra setup needed. */
}

int terminal_consume_resize(void) {
    if (g_resize_pending) {
        g_resize_pending = 0;
        return 1;
    }
    return 0;
}

/* ---- Cleanup handlers ---- */

void terminal_install_cleanup_handlers(TermRawState *state) {
    g_cleanup_state = state;
    SetConsoleCtrlHandler(ctrl_handler, TRUE);
}
