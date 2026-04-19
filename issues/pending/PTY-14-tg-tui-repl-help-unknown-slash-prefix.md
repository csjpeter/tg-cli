# PTY-14 — PTY test: tg-tui REPL help, unknown command, and '/' prefix

## Gap
`tg_tui.c` REPL has branches that no PTY test exercises:

1. `help` / `?` → prints multi-line help output.  No test confirms
   the help lines render correctly (not interleaved with the
   `tg> ` prompt due to a missing newline).
2. Any unknown command (e.g. `foo`) → prints
   `unknown command: foo (try 'help')`.  No test pins this string
   or the exit-to-prompt behaviour.
3. `/me`, `/dialogs`, etc. — IRC-style leading `/` is stripped
   (`*cmd == '/'` check).  No test verifies `/me` and `me` produce
   identical output.

## Scope
Add `tests/functional/pty/test_tg_tui_repl_misc.c`:
- Case 1: send `help\n` → master contains "Read commands:",
  "Write commands:", "Session:" within 1 s.
- Case 2: send `foo\n` → master contains "unknown command: foo".
- Case 3: run `/me\n` with mock returning a user; assert same output
  as `me\n`.
- Case 4: send `?\n` → same content as `help\n`.
- Final: send `quit\n` → exit 0.

## Acceptance
- 4 sub-cases pass.
- No extra prompt noise between help lines.

## Dependencies
- PTY-01.
