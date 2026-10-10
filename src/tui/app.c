/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-17 */
/* TUI state machine implementation. */

/**
 * @file tui/app.c
 * @brief TUI state machine implementation (US-11 v2).
 */

#include "tui/app.h"

#include <string.h>

int tui_app_init(TuiApp *app, int rows, int cols) {
    if (!app) return -1;
    memset(app, 0, sizeof(*app));
    app->screen = tui_screen_new(rows, cols);
    if (!app->screen) return -1;
    layout_compute(&app->layout, rows, cols, 28);
    dialog_pane_init(&app->dialogs);
    history_pane_init(&app->history);
    status_row_init(&app->status);
    app->focus = TUI_FOCUS_DIALOGS;
    app->rows = rows;
    app->cols = cols;
    /* Wire viewport heights so list_views know how much they render. */
    tui_listview_set_viewport(&app->dialogs.lv, app->layout.dialogs.rows);
    tui_listview_set_viewport(&app->history.lv, app->layout.history.rows);
    return 0;
}

void tui_app_free(TuiApp *app) {
    if (!app) return;
    tui_screen_free(app->screen);
    app->screen = NULL;
}

int tui_app_resize(TuiApp *app, int rows, int cols) {
    if (!app) return -1;
    if (!app->screen || tui_screen_resize(app->screen, rows, cols) != 0)
        return -1;
    layout_compute(&app->layout, rows, cols, 28);
    tui_listview_set_viewport(&app->dialogs.lv, app->layout.dialogs.rows);
    tui_listview_set_viewport(&app->history.lv, app->layout.history.rows);
    app->rows = rows;
    app->cols = cols;
    return 0;
}

static tui_listview *focused_list_view(TuiApp *app) {
    return (app->focus == TUI_FOCUS_DIALOGS)
         ? &app->dialogs.lv
         : &app->history.lv;
}

static TuiEvent focus_to(TuiApp *app, TuiFocus f, StatusMode m) {
    if (app->focus == f) return TUI_EVENT_NONE;
    app->focus = f;
    app->status.mode = m;
    return TUI_EVENT_REDRAW;
}

TuiEvent tui_app_handle_key(TuiApp *app, const tui_event *ev) {
    if (!app || !ev || ev->type != TUI_EV_KEY) return TUI_EVENT_NONE;

    if (ev->key == TUI_KEY_ESC || TUI_EV_IS_CTRL(ev, 'c')
        || TUI_EV_IS_CHAR(ev, 'q') || TUI_EV_IS_CHAR(ev, 'Q'))
        return TUI_EVENT_QUIT;

    if ((ev->key == TUI_KEY_LEFT && ev->mods == 0) || TUI_EV_IS_CHAR(ev, 'h'))
        return focus_to(app, TUI_FOCUS_DIALOGS, STATUS_MODE_DIALOGS);
    if ((ev->key == TUI_KEY_RIGHT && ev->mods == 0) || TUI_EV_IS_CHAR(ev, 'l'))
        return focus_to(app, TUI_FOCUS_HISTORY, STATUS_MODE_HISTORY);

    if (ev->key == TUI_KEY_ENTER && ev->mods == 0) {
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
    }

    if (tui_listview_handle(focused_list_view(app), ev))
        return TUI_EVENT_REDRAW;
    return TUI_EVENT_NONE;
}

void tui_app_paint(TuiApp *app) {
    if (!app) return;
    tui_screen_clear(app->screen, TUI_STYLE_DEFAULT);
    if (pane_is_valid(&app->layout.dialogs)) {
        dialog_pane_render(&app->dialogs, &app->layout.dialogs,
                           app->screen,
                           app->focus == TUI_FOCUS_DIALOGS);
    }
    if (pane_is_valid(&app->layout.history)) {
        history_pane_render(&app->history, &app->layout.history,
                            app->screen);
    }
    if (pane_is_valid(&app->layout.status)) {
        status_row_render(&app->status, &app->layout.status,
                          app->screen);
    }
}
