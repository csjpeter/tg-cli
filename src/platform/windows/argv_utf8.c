/* SPDX-License-Identifier: MIT */
/* Copyright 2026 Peter Csaszar */

/**
 * @file platform/windows/argv_utf8.c
 * @brief Re-encode argv from system ANSI codepage to UTF-8 on Windows/MinGW.
 *
 * MinGW's CRT passes argv in the system ANSI codepage (e.g. CP1252), not
 * UTF-8.  Telegram usernames, group names, and message text regularly contain
 * non-ASCII Unicode characters.  This module re-reads the command line via
 * GetCommandLineW() + CommandLineToArgvW(), converts each wide-char argument
 * to UTF-8 with WideCharToMultiByte(CP_UTF8, ...), and replaces the caller's
 * argc/argv pointers before any argument parsing begins.
 *
 * SetConsoleCP / SetConsoleOutputCP are also set to CP_UTF8 so that stdin
 * and stdout handle UTF-8 correctly in a Windows terminal.
 */

#include "../path.h"

/* Ensure we get the full Win32 API surface without legacy compat macros. */
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>   /* CommandLineToArgvW */
#include <stdlib.h>
#include <stdio.h>

void platform_normalize_argv(int *argc, char ***argv)
{
    /* Switch console code pages to UTF-8 so that non-ASCII output is rendered
     * correctly in Windows Terminal / modern cmd.exe. */
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    /* Obtain the full command line as UTF-16. */
    LPWSTR cmdline = GetCommandLineW();
    if (!cmdline) return;

    int wargc = 0;
    LPWSTR *wargv = CommandLineToArgvW(cmdline, &wargc);
    if (!wargv) return;

    /* Allocate a new argv array of UTF-8 strings. */
    char **utf8_argv = (char **)malloc((size_t)(wargc + 1) * sizeof(char *));
    if (!utf8_argv) {
        LocalFree(wargv);
        return;
    }

    for (int i = 0; i < wargc; i++) {
        /* Query the required buffer size first (passing 0 as cbMultiByte). */
        int needed = WideCharToMultiByte(CP_UTF8, 0,
                                         wargv[i], -1,
                                         NULL, 0,
                                         NULL, NULL);
        if (needed <= 0) {
            /* Fallback: copy an empty string so argv stays valid. */
            utf8_argv[i] = (char *)malloc(1);
            if (utf8_argv[i]) utf8_argv[i][0] = '\0';
            continue;
        }

        utf8_argv[i] = (char *)malloc((size_t)needed);
        if (!utf8_argv[i]) {
            /* Out of memory — leave remaining slots NULL and abort loop. */
            for (int j = i + 1; j < wargc; j++) utf8_argv[j] = NULL;
            wargc = i;
            break;
        }

        WideCharToMultiByte(CP_UTF8, 0,
                            wargv[i], -1,
                            utf8_argv[i], needed,
                            NULL, NULL);
    }
    utf8_argv[wargc] = NULL;   /* POSIX-style NULL terminator */

    LocalFree(wargv);

    *argc = wargc;
    *argv = utf8_argv;
}
