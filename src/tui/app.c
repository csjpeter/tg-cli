/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

/**
 * @file tui/app.c
 * @brief TUI state machine implementation (US-11 v2).
 */

#include "tui/app.h"

#include <string.h>

int tui_app_init(TuiApp *app, int rows, int cols) {
    if (!app) return -1;
    memset(app, 0, sizeof(*app));
    if (screen_init(&app->screen, rows, cols) != 0) return -1;
    layout_compute(&app->layout, rows, cols, 28);
    dialog_pane_init(&app->dialogs);
    history_pane_init(&app->history);
    status_row_init(&app->status);
    app->focus = TUI_FOCUS_DIALOGS;
    app->rows = rows;
    app->cols = cols;
    /* Wire viewport heights so list_views know how much they render. */
    app->dialogs.lv.rows_visible = app->layout.dialogs.rows;
    app->history.lv.rows_visible = app->layout.history.rows;
    return 0;
}

void tui_app_free(TuiApp *app) {
    if (!app) return;
    screen_free(&app->screen);
}

int tui_app_resize(TuiApp *app, int rows, int cols) {
    if (!app) return -1;
    screen_free(&app->screen);
    if (screen_init(&app->screen, rows, cols) != 0) return -1;
    layout_compute(&app->layout, rows, cols, 28);
    list_view_set_viewport(&app->dialogs.lv, app->layout.dialogs.rows);
    list_view_set_viewport(&app->history.lv, app->layout.history.rows);
    app->rows = rows;
    app->cols = cols;
    return 0;
}

static ListView *focused_list_view(TuiApp *app) {
    return (app->focus == TUI_FOCUS_DIALOGS)
         ? &app->dialogs.lv
         : &app->history.lv;
}

TuiEvent tui_app_handle_key(TuiApp *app, TermKey key) {
    if (!app) return TUI_EVENT_NONE;

    switch (key) {
    case TERM_KEY_QUIT:
    case TERM_KEY_ESC:
        return TUI_EVENT_QUIT;

    case TERM_KEY_LEFT:
        if (app->focus != TUI_FOCUS_DIALOGS) {
            app->focus = TUI_FOCUS_DIALOGS;
            app->status.mode = STATUS_MODE_DIALOGS;
            return TUI_EVENT_REDRAW;
        }
        return TUI_EVENT_NONE;

    case TERM_KEY_RIGHT:
        if (app->focus != TUI_FOCUS_HISTORY) {
            app->focus = TUI_FOCUS_HISTORY;
            app->status.mode = STATUS_MODE_HISTORY;
            return TUI_EVENT_REDRAW;
        }
        return TUI_EVENT_NONE;

    case TERM_KEY_PREV_LINE:
        list_view_move_up(focused_list_view(app));
        return TUI_EVENT_REDRAW;

    case TERM_KEY_NEXT_LINE:
        list_view_move_down(focused_list_view(app));
        return TUI_EVENT_REDRAW;

    case TERM_KEY_PREV_PAGE:
        list_view_page_up(focused_list_view(app));
        return TUI_EVENT_REDRAW;

    case TERM_KEY_NEXT_PAGE:
        list_view_page_down(focused_list_view(app));
        return TUI_EVENT_REDRAW;

    case TERM_KEY_HOME:
        list_view_home(focused_list_view(app));
        return TUI_EVENT_REDRAW;

    case TERM_KEY_END:
        list_view_end(focused_list_view(app));
        return TUI_EVENT_REDRAW;

    case TERM_KEY_ENTER:
        if (app->focus == TUI_FOCUS_DIALOGS
            && dialog_pane_selected(&app->dialogs) != NULL) {
            /* Caller loads history for the selection and then flips focus
             * to the history pane. We switch focus here pre-emptively so
             * the status-row hint updates even before the network call. */
            app->focus = TUI_FOCUS_HISTORY;
            app->status.mode = STATUS_MODE_HISTORY;
            return TUI_EVENT_OPEN_DIALOG;
        }
        return TUI_EVENT_NONE;

    default:
        return TUI_EVENT_NONE;
    }
}

TuiEvent tui_app_handle_char(TuiApp *app, int ch) {
    if (!app) return TUI_EVENT_NONE;
    switch (ch) {
    case 'q': case 'Q':
        return TUI_EVENT_QUIT;
    case 'j':
        return tui_app_handle_key(app, TERM_KEY_NEXT_LINE);
    case 'k':
        return tui_app_handle_key(app, TERM_KEY_PREV_LINE);
    case 'h':
        return tui_app_handle_key(app, TERM_KEY_LEFT);
    case 'l':
        return tui_app_handle_key(app, TERM_KEY_RIGHT);
    case 'g':
        return tui_app_handle_key(app, TERM_KEY_HOME);
    case 'G':
        return tui_app_handle_key(app, TERM_KEY_END);
    default:
        return TUI_EVENT_NONE;
    }
}

void tui_app_paint(TuiApp *app) {
    if (!app) return;
    screen_clear_back(&app->screen);
    if (pane_is_valid(&app->layout.dialogs)) {
        dialog_pane_render(&app->dialogs, &app->layout.dialogs,
                           &app->screen,
                           app->focus == TUI_FOCUS_DIALOGS);
    }
    if (pane_is_valid(&app->layout.history)) {
        history_pane_render(&app->history, &app->layout.history,
                            &app->screen);
    }
    if (pane_is_valid(&app->layout.status)) {
        status_row_render(&app->status, &app->layout.status,
                          &app->screen);
    }
}
