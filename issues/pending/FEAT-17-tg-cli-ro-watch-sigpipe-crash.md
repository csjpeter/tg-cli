# FEAT-17 — tg-cli-ro watch crashes on SIGPIPE when pipe consumer exits

## Gap
`tg-cli-ro watch` is designed for streaming output to pipes:
```
tg-cli-ro watch --json | head -n 10
```
When `head` receives 10 lines and exits, the pipe is broken. The next
`printf` / `fwrite` in `cmd_watch` raises SIGPIPE, which terminates
the process with signal 13 (no cleanup, no friendly error message). The
exit status is 128+13 = 141, not the documented 0 or 1.

No `signal(SIGPIPE, SIG_IGN)` or equivalent is registered anywhere in
the `src/` tree.

## Scope
1. In `src/main/tg_cli_ro.c:cmd_watch()`, call
   `signal(SIGPIPE, SIG_IGN)` before the poll loop (immediately after
   the `signal(SIGINT, on_sigint)` call on line 172).
2. Check the return value of every `printf`/`fflush(stdout)` in the
   watch loop; on write error (`errno == EPIPE`), set `g_stop = 1`
   and break.
3. Exit with code 0 (normal termination requested by downstream) in
   this case.
4. Document in `man/tg-cli-ro.1` SIGNALS section (DOC-12): SIGPIPE is
   ignored; broken-pipe is detected via write error and causes a clean
   exit.
5. Unit/functional test: write to a closed pipe fd and assert the watch
   loop terminates cleanly rather than crashing.

## Acceptance
- `tg-cli-ro watch --json | head -n 1` exits with status 0 after the
  first JSON line is consumed.
- No `Broken pipe` messages on stderr.
