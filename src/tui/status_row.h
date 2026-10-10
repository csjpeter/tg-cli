/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-17 */
/* Bottom status line for the TUI. */

#ifndef TUI_STATUS_ROW_H
#define TUI_STATUS_ROW_H

/**
 * @file tui/status_row.h
 * @brief Bottom status line for the TUI (US-11 v2).
 *
 * The status row is one row tall and full width. It shows the key hints of
 * the focused pane, or, when there is a message, the message instead of the
 * hints until the next keypress clears it (policies/tui-interface-model.md,
 * "One status line"). A message that does not fit is cut; the hints are the
 * ones that fit by construction. The row keeps the terminal's own
 * background: hints are yellow, a progress message italic yellow, an error
 * bold red, a confirmation bold green.
 */

#include "tui/pane.h"

#define STATUS_ROW_MSG_MAX 128

typedef enum {
    STATUS_MODE_DIALOGS = 0,  /* arrows/j/k scroll dialogs, Enter opens */
    STATUS_MODE_HISTORY = 1,  /* arrows scroll history, Tab returns    */
} StatusMode;

/** What a message means; it picks the style it is drawn in. */
typedef enum {
    STATUS_MSG_PLAIN = 0,     /* the terminal's default style  */
    STATUS_MSG_PROGRESS,      /* waiting for something         */
    STATUS_MSG_OK,            /* a write that worked           */
    STATUS_MSG_ERROR,         /* a refusal or a failure        */
} StatusMsgKind;

typedef struct {
    StatusMode    mode;
    StatusMsgKind kind;
    char          message[STATUS_ROW_MSG_MAX];  /* replaces the hints; may be empty */
} StatusRow;

/** Zero state (mode = dialogs, message empty). */
void status_row_init(StatusRow *sr);

/** Overwrite the message string (plain style); truncates to
 *  STATUS_ROW_MSG_MAX - 1. NULL clears it. */
void status_row_set_message(StatusRow *sr, const char *msg);

/** As status_row_set_message(), in the style of @p kind. */
void status_row_set_message_kind(StatusRow *sr, const char *msg,
                                 StatusMsgKind kind);

/** The hint text of @p mode (the keys of that view). */
const char *status_row_hint(StatusMode mode);

/** Render the row onto @p screen within @p pane. */
void status_row_render(const StatusRow *sr,
                        const tui_rect *pane, tui_screen *screen);

#endif /* TUI_STATUS_ROW_H */
