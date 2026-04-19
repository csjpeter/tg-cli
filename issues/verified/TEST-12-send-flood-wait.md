# TEST-12 — functional test for FLOOD_WAIT_X on send

## Gap
US-12 acceptance:
> Handles `FLOOD_WAIT_X` by surfacing a clear error and exit code.

`test_write_path.c test_send_message_rpc_error` covers a generic
error, not specifically FLOOD_WAIT_X with its retry-after payload.

## Scope
1. Responder for `messages.sendMessage` returns
   `rpc_error(420, "FLOOD_WAIT_30")`.
2. Assert `domain_send_message` returns non-zero and surfaces the
   30-second hint to stderr (captured via `freopen`).
3. Assert we do NOT auto-retry (FLOOD_WAIT is user-visible by
   design).

## Acceptance
- Test green; error message includes "FLOOD_WAIT_30" and "30".
