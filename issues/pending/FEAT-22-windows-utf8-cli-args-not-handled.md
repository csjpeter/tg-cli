# FEAT-22 — Windows build: CLI arguments not converted from CP1252/UTF-16 to UTF-8

## Category
Feature / Portability

## Severity
Medium

## Finding
On Windows with MinGW, `main(int argc, char **argv)` receives arguments in the
system ANSI codepage (typically CP1252 or another regional codepage), not UTF-8.
Telegram usernames, group names, and message text regularly contain non-ASCII
Unicode characters (Cyrillic, Arabic, CJK, etc.).

The `src/platform/windows/` directory provides `terminal.c`, `path.c`, and
`socket.c` but there is no UTF-8 argument bootstrap.  The POSIX main files
(`src/main/tg_cli_ro.c`, `tg_tui.c`, `tg_cli.c`) use `int main(int argc,
char **argv)` with no codepage conversion.

The canonical fix for MinGW is to call `SetConsoleOutputCP(CP_UTF8)` and
`SetConsoleCP(CP_UTF8)`, use `wmain` with `WideCharToMultiByte`, or link a
UTF-8 manifest.

## Evidence
- `/home/csjpeter/ai-projects/tg-cli/src/main/tg_cli_ro.c` — `int main(int argc, char **argv)`, no `wmain` or codepage call
- `/home/csjpeter/ai-projects/tg-cli/src/platform/windows/` — no `argv_utf8.c` or similar

## Fix
1. Add `src/platform/windows/main_utf8.c` that provides a `wmain` shim converting
   `wargv[]` to a UTF-8 `argv[]` using `WideCharToMultiByte(CP_UTF8, ...)`.
2. Or add `SetConsoleOutputCP(CP_UTF8)` + `_setmode(_fileno(stdout), _O_U8TEXT)` in
   the Windows bootstrap path.
3. Add `SetConsoleOutputCP(CP_UTF8)` call in platform bootstrap called from
   `app_bootstrap()` on Windows.
