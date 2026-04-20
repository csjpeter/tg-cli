/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file readline.h
 * @brief Custom interactive line editor built on top of terminal.h.
 *
 * Features:
 *   - Cursor movement (Left / Right / Home / End / Ctrl-A / Ctrl-E)
 *   - Character insertion and deletion (Backspace / Delete / Ctrl-D)
 *   - Word deletion (Ctrl-W)
 *   - Kill to end of line (Ctrl-K)
 *   - History navigation (Up / Down arrows)
 *   - Prompt display with live redraw
 *   - Works transparently in non-TTY mode (falls back to fgets)
 *
 * The editor does NOT take ownership of the history buffer — the caller
 * manages storage.  A typical use-case:
 *
 *   LineHistory hist;
 *   rl_history_init(&hist);
 *   char buf[1024];
 *   int rc = rl_readline(">> ", buf, sizeof(buf), &hist);
 *   if (rc > 0) rl_history_add(&hist, buf);
 */

#ifndef READLINE_H
#define READLINE_H

#include <stddef.h>

/** Maximum number of history entries kept in memory. */
#define RL_HISTORY_MAX 256

/** Maximum length of a single history entry (bytes). */
#define RL_HISTORY_ENTRY_MAX 4096

/**
 * @brief Circular history buffer.
 *
 * Managed by rl_history_init() / rl_history_add().
 * Entries are stored as NUL-terminated strings.
 */
typedef struct {
    char  entries[RL_HISTORY_MAX][RL_HISTORY_ENTRY_MAX]; /**< Storage.        */
    int   head;    /**< Index of the oldest entry (write position).            */
    int   count;   /**< Number of valid entries (0 .. RL_HISTORY_MAX).         */
} LineHistory;

/**
 * @brief Initialise a LineHistory struct to empty. */
void rl_history_init(LineHistory *h);

/**
 * @brief Append a line to the history (oldest entry evicted when full).
 *
 * Empty strings are silently ignored.
 * Duplicate of the most recent entry is silently ignored.
 *
 * @param h    History buffer.
 * @param line NUL-terminated string to add.
 */
void rl_history_add(LineHistory *h, const char *line);

/**
 * @brief Read a line from the user with interactive line editing.
 *
 * Displays @p prompt, enters raw terminal mode, and processes keystrokes
 * until Enter or Ctrl-C is pressed.
 *
 * On non-TTY stdin (e.g. pipe), falls back to a simple fgets() call
 * without echo manipulation or raw mode.
 *
 * @param prompt   String displayed before the input area (not stored).
 * @param buf      Output buffer for the line (NUL-terminated, no newline).
 * @param size     Size of @p buf in bytes (including NUL terminator).
 * @param history  Optional history buffer (may be NULL for no history).
 * @return Number of characters in the line (>= 0), or -1 on Ctrl-C / EOF.
 */
int rl_readline(const char *prompt, char *buf, size_t size,
                LineHistory *history);

#endif /* READLINE_H */
