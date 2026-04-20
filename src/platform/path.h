/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Peter Csaszar */

#ifndef PLATFORM_PATH_H
#define PLATFORM_PATH_H

/**
 * @file path.h
 * @brief Platform-specific base directory resolution.
 */

/**
 * Returns the user's home directory path.
 * POSIX: $HOME or getpwuid(getuid())->pw_dir
 * Windows: %USERPROFILE%
 * The returned string is owned by the platform layer (do not free).
 * Returns NULL if the home directory cannot be determined.
 */
const char *platform_home_dir(void);

/**
 * Returns the base directory for user-specific cache files.
 * POSIX: $XDG_CACHE_HOME or ~/.cache
 * Windows: %LOCALAPPDATA%
 * The returned string is owned by the platform layer (do not free).
 */
const char *platform_cache_dir(void);

/**
 * Returns the base directory for user-specific configuration files.
 * POSIX: $XDG_CONFIG_HOME or ~/.config
 * Windows: %APPDATA%
 * The returned string is owned by the platform layer (do not free).
 */
const char *platform_config_dir(void);

/**
 * Normalises command-line arguments to UTF-8.
 *
 * On POSIX the function is a no-op (argv is already UTF-8 by convention).
 * On Windows (MinGW) main() receives arguments in the system ANSI codepage
 * (e.g. CP1252).  This function re-reads the command line via
 * GetCommandLineW() + CommandLineToArgvW(), converts every wide-char argument
 * to UTF-8 with WideCharToMultiByte(CP_UTF8), and replaces *argc / *argv with
 * freshly allocated UTF-8 strings.
 *
 * The replacement strings are allocated with malloc() and are never freed
 * (they live for the entire process lifetime).  *argc is also updated in case
 * CommandLineToArgvW() disagrees with the CRT count.
 *
 * Call this as the very first statement inside main(), before any argument
 * parsing.
 */
void platform_normalize_argv(int *argc, char ***argv);

#endif /* PLATFORM_PATH_H */
