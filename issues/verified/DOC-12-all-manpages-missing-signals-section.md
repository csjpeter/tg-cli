# DOC-12 — All three man pages missing a SIGNALS section

## Gap
None of the three man pages document signal behaviour, but the binaries
have meaningful (and divergent) signal semantics:

| Binary | Signal | Behaviour |
|--------|--------|-----------|
| `tg-cli-ro` | SIGINT | Sets `g_stop = 1` in `cmd_watch`; terminates the poll loop cleanly (`tg_cli_ro.c:172`). All other subcommands use the default handler. |
| `tg-cli-ro` | SIGPIPE | Default (process terminates) — piping `watch` output to `head` and letting `head` exit will crash the process mid-stream. |
| `tg-tui` | SIGWINCH | Wakes `terminal_wait_key()`; `run_tui_loop` calls `tui_app_resize` and repaints (`tg_tui.c:746-758`). |
| `tg-tui` | SIGTERM | No handler — terminal left in raw mode on forced kill. |
| `tg-cli` | All | Default handlers throughout. |

The missing SIGNALS section leaves users without guidance on how to
interrupt `watch`, resize the TUI, or safely kill the process.

## Scope
1. Add `.SH SIGNALS` to `man/tg-cli-ro.1` documenting SIGINT
   (terminates `watch` gracefully) and noting SIGPIPE behaviour.
2. Add `.SH SIGNALS` to `man/tg-tui.1` documenting SIGWINCH
   (live resize) and SIGTERM (leaves terminal in raw mode if `--tui`
   is active — see FEAT-16 for the fix).
3. Add `.SH SIGNALS` to `man/tg-cli.1` noting no custom handlers are
   registered (default POSIX behaviour applies).

## Acceptance
- Each man page has a `.SH SIGNALS` section that accurately reflects the
  current code behaviour.
- No section contradicts the FEAT-16 plan for SIGTERM handling.
- `groff -man` on all three files produces no warnings.
