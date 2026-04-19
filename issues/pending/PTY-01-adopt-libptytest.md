# PTY-01 — Adopt libptytest into tg-cli for TTY-backed TUI tests

## Gap
The sibling project `/home/csjpeter/ai-projects/email-cli` contains a
ready-made PTY test library at `libs/libptytest/` (`ptytest.h`,
`pty_session.c`, `pty_screen.c`, `pty_sync.c`, `pty_assert.h`,
`pty_internal.h`). It forks and execs a program inside a pseudo-terminal,
sends keystrokes, and inspects the virtual screen buffer with a VT100
parser.

`tg-cli` has **zero** TTY-backed functional tests. US-11 acceptance
("Starts without full-screen flicker; restores terminal cleanly on
SIGINT") and US-02 ("TUI modules become testable headlessly") both
require either headless or PTY-backed tests. The existing
`tests/functional/test_tui_e2e.c` uses `/dev/null` instead of a real
PTY, so actual rendering and terminal-mode behaviour are never verified.

**Do NOT introduce a cross-project dependency.** Copy the library files
into `tg-cli`.

## Scope
1. Copy the following files from `email-cli/libs/libptytest/` into
   `tg-cli/libs/libptytest/`:
   - `ptytest.h`
   - `pty_assert.h`
   - `pty_internal.h`
   - `pty_session.c`
   - `pty_screen.c`
   - `pty_sync.c`
2. Create `tg-cli/libs/libptytest/CMakeLists.txt`:
   ```cmake
   add_library(ptytest STATIC pty_session.c pty_screen.c pty_sync.c)
   target_include_directories(ptytest PUBLIC .)
   ```
3. In `tests/functional/CMakeLists.txt`, add a new `pty` subdirectory
   target (`tests/functional/pty/`) that links `ptytest` and the
   `tg-tui` binary (or its test-stub), enabled only on POSIX
   (`UNIX` cmake check).
4. Add a smoke test `tests/functional/pty/test_pty_smoke.c` that
   opens a PTY session and verifies the harness itself works (no tg-cli
   binary needed yet).

## Acceptance
- `./manage.sh test` builds and runs the PTY smoke test on Linux.
- The PTY target is skipped gracefully on Windows (non-POSIX build).
- No test in `tests/functional/pty/` introduces a cross-project path
  dependency.
