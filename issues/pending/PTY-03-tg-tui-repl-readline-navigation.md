# PTY-03 — PTY test: tg-tui REPL readline cursor movement and editing

## Gap
US-02 ("TUI modules become testable headlessly") and the readline
implementation (`src/core/readline.c`) have unit tests for the state
machine, but no test verifies the actual byte sequences emitted to a
real TTY or that cursor-movement keypresses are accepted and reflected
on screen.

Key acceptance criteria that are untested at the TTY level:
- Left/right arrows move the cursor; Home/End jump.
- `Ctrl-K` kills to end of line.
- Up/Down arrows cycle history.
- `Ctrl-R` initiates reverse-search.
- The prompt `tg> ` is visible on the first line.

Depends on PTY-01 (libptytest).

## Scope
Add `tests/functional/pty/test_repl_readline.c`:
1. Launch `tg-tui` (REPL mode, no `--tui`) with a pre-seeded session.
2. Wait for the `tg> ` prompt via `pty_wait_for(s, "tg> ", 3000)`.
3. Send `hello` + Left×3 + `x` (inserts 'x' before 'lo') + Enter.
4. Verify that the resulting command string is `helloxx... ` or that
   the echo line shows the expected edited text.
5. Send `Ctrl-D`; assert child exits 0.

Test should also cover:
- `Ctrl-K` clears the rest of the line (send partial text, Ctrl-K, assert truncation).
- Up arrow recalls last history entry.

## Acceptance
- All assertions pass under ASAN.
- No timeout in the PTY wait calls.
- Test completes in under 5 seconds.
