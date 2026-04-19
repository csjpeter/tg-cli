# TEST-15 — error paths for edit / delete / forward

## Gap
US-13 acceptance:
> `MESSAGE_ID_INVALID`, `MESSAGE_AUTHOR_REQUIRED`, `PEER_ID_INVALID`
> surface with non-zero exit and a clear diagnostic to stderr.

No functional test covers these. We only assert generic "RPC error"
behaviour.

## Scope
Three new cases in `test_write_path.c`:
1. `edit_message_id_invalid` — responder returns
   `rpc_error(400, "MESSAGE_ID_INVALID")`; assert non-zero and
   stderr mentions it.
2. `edit_message_author_required` — 403 /
   `MESSAGE_AUTHOR_REQUIRED`.
3. `delete_peer_id_invalid` — 400 / `PEER_ID_INVALID`.

## Acceptance
- Three tests green; error-surfacing code verified.
