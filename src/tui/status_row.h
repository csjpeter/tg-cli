/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

#ifndef TUI_STATUS_ROW_H
#define TUI_STATUS_ROW_H

/**
 * @file tui/status_row.h
 * @brief Bottom status line for the TUI (US-11 v2).
 *
 * The status row shows a short context hint (currently-focused pane +
 * key reminders) on the left and an arbitrary message (e.g. "loading…",
 * last error) on the right. It is one row tall and always full-width.
 */

#include "tui/pane.h"
#include "tui/screen.h"

#define STATUS_ROW_MSG_MAX 128

typedef enum {
    STATUS_MODE_DIALOGS = 0,  /* arrows/j/k scroll dialogs, Enter opens */
    STATUS_MODE_HISTORY = 1,  /* arrows scroll history, Tab returns    */
} StatusMode;

typedef struct {
    StatusMode mode;
    char       message[STATUS_ROW_MSG_MAX];  /* right-aligned, may be empty */
} StatusRow;

/** Zero state (mode = dialogs, message empty). */
void status_row_init(StatusRow *sr);

/** Overwrite the message string; truncates to STATUS_ROW_MSG_MAX - 1. */
void status_row_set_message(StatusRow *sr, const char *msg);

/** Render the row onto @p screen within @p pane. Inverse-video across the
 *  whole row to make the status line visually distinct. */
void status_row_render(const StatusRow *sr,
                        const Pane *pane, Screen *screen);

#endif /* TUI_STATUS_ROW_H */
