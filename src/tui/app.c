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

#include <stdio.h>
#include <string.h>

#define TUI_PROGRAM_NAME "tg-tui"

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
    if (app->help_open) tui_dialog_free(&app->help);
    app->help_open = 0;
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

/* The help overlay: every key of the view, in two columns. */
static const char *const DIALOGS_HELP[] = {
    "j, Down",         "next dialog",
    "k, Up",           "previous dialog",
    "ctrl-f, PgDn",    "page down",
    "ctrl-b, PgUp",    "page up",
    "g, Home",         "first dialog",
    "G, End",          "last dialog",
    "Enter, l, Right", "open the dialog's history",
    "r",               "reload the dialogs",
    "?",               "this help",
    "Esc",             "quit",
    "ctrl-c",          "quit at once",
};
static const char *const HISTORY_HELP[] = {
    "j, Down",         "next message",
    "k, Up",           "previous message",
    "ctrl-f, PgDn",    "page down",
    "ctrl-b, PgUp",    "page up",
    "g, Home",         "first message",
    "G, End",          "last message",
    "h, Left",         "back to the dialogs",
    "r",               "reload the messages",
    "?",               "this help",
    "Esc, q, Backspace", "back to the dialogs",
    "ctrl-c",          "quit at once",
};

static void open_help(TuiApp *app) {
    int hist = app->focus == TUI_FOCUS_HISTORY;
    const char *const *pairs = hist ? HISTORY_HELP : DIALOGS_HELP;
    int n = (int)((hist ? sizeof(HISTORY_HELP) : sizeof(DIALOGS_HELP))
                  / sizeof(char *) / 2);
    if (tui_dialog_help(&app->help,
                        hist ? "Message history shortcuts"
                             : "Dialog list shortcuts",
                        pairs, n) == 0)
        app->help_open = 1;
}

/* Back one level: history -> dialogs. Returns 0 on the top level. */
static int go_back(TuiApp *app) {
    if (app->focus == TUI_FOCUS_DIALOGS) return 0;
    focus_to(app, TUI_FOCUS_DIALOGS, STATUS_MODE_DIALOGS);
    return 1;
}

static TuiEvent handle_key(TuiApp *app, const tui_event *ev) {
    if (TUI_EV_IS_CTRL(ev, 'c')) return TUI_EVENT_QUIT;

    /* A modal takes every keypress until it closes. */
    if (app->help_open) {
        if (tui_dialog_handle(&app->help, ev) != TUI_DIALOG_OPEN) {
            tui_dialog_free(&app->help);
            app->help_open = 0;
        }
        return TUI_EVENT_REDRAW;
    }

    if (ev->key == TUI_KEY_ESC && ev->mods == 0)
        return go_back(app) ? TUI_EVENT_REDRAW : TUI_EVENT_QUIT;
    if ((TUI_EV_IS_CHAR(ev, 'q') && ev->mods == 0)
        || (ev->key == TUI_KEY_BACKSPACE && ev->mods == 0))
        return go_back(app) ? TUI_EVENT_REDRAW : TUI_EVENT_NONE;

    if (TUI_EV_IS_CHAR(ev, '?')) {
        open_help(app);
        return app->help_open ? TUI_EVENT_REDRAW : TUI_EVENT_NONE;
    }
    if (TUI_EV_IS_CHAR(ev, 'r') && ev->mods == 0) return TUI_EVENT_RELOAD;

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

TuiEvent tui_app_handle_key(TuiApp *app, const tui_event *ev) {
    if (!app || !ev || ev->type != TUI_EV_KEY) return TUI_EVENT_NONE;

    /* A message stays until the next keypress, which clears it. */
    int had_message = app->status.message[0] != '\0';
    status_row_set_message(&app->status, NULL);

    TuiEvent r = handle_key(app, ev);
    return (r == TUI_EVENT_NONE && had_message) ? TUI_EVENT_REDRAW : r;
}

/* The header bar: where we are on the left, the program's name on the
 * right; the left side is clipped before the name. */
static void paint_header(const TuiApp *app) {
    const tui_rect *h = &app->layout.header;
    tui_canvas c = tui_canvas_sub(tui_canvas_of(app->screen), *h);
    tui_canvas_fill(c, tui_canvas_rect(c), TUI_STYLE_HEADER);

    char left[96];
    const tui_listview *lv = app->focus == TUI_FOCUS_DIALOGS
                           ? &app->dialogs.lv : &app->history.lv;
    const char *what = app->focus == TUI_FOCUS_DIALOGS ? "Dialogs" : "Messages";
    if (lv->count > 0)
        snprintf(left, sizeof(left), "%s \xc2\xb7 %d of %d", what,
                 lv->selected + 1, lv->count);
    else
        snprintf(left, sizeof(left), "%s \xc2\xb7 0", what);

    int name_w = (int)strlen(TUI_PROGRAM_NAME);
    int name_col = h->cols - name_w - 1;
    if (name_col < 1) name_col = 1;
    tui_canvas lc = tui_canvas_sub(c, tui_rect_make(0, 1, 1,
                                   name_col > 2 ? name_col - 2 : 0));
    tui_canvas_put(lc, 0, 0, left, TUI_STYLE_HEADER);
    tui_canvas_put(c, 0, name_col, TUI_PROGRAM_NAME, TUI_STYLE_HEADER);
}

void tui_app_paint(TuiApp *app) {
    if (!app) return;
    tui_screen_clear(app->screen, TUI_STYLE_DEFAULT);
    if (!pane_is_valid(&app->layout.dialogs)) {
        /* Too small to draw the panes: a short notice, not a crash. */
        tui_canvas_put(tui_canvas_of(app->screen), 0, 0,
                       "terminal too small", TUI_STYLE_ERROR);
        return;
    }
    if (pane_is_valid(&app->layout.header))
        paint_header(app);
    dialog_pane_render(&app->dialogs, &app->layout.dialogs,
                       app->screen,
                       app->focus == TUI_FOCUS_DIALOGS);
    if (pane_is_valid(&app->layout.history)) {
        history_pane_render(&app->history, &app->layout.history,
                            app->screen,
                            app->focus == TUI_FOCUS_HISTORY);
    }
    if (pane_is_valid(&app->layout.status)) {
        status_row_render(&app->status, &app->layout.status,
                          app->screen);
    }
    if (app->help_open)
        tui_dialog_render(&app->help, tui_canvas_of(app->screen));
}
