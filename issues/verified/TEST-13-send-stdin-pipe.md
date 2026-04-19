# TEST-13 — functional test for `send --stdin`

## Gap
US-12 / P8-03:
> `echo "msg from stdin" | tg-cli send @peer`

`arg_parse.c` sets `args.stdin_input = 1` when `--stdin` is seen, but
no functional test drives the actual stdin read + `sendMessage` wire.

## Scope
1. Redirect a pipe/fd into stdin (or use `fmemopen` into `stdin` via
   `freopen`) with the string "hello from pipe\n".
2. Call `cmd_send` with `args.stdin_input = 1`.
3. Responder inspects the `message` TL string and asserts the
   bytes match.

## Acceptance
- Test green; covers the stdin branch in `src/main/tg_cli.c cmd_send`.
