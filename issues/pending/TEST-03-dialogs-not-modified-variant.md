# TEST-03 — functional test for messages.dialogsNotModified

## Gap
Same US-04 acceptance as TEST-02, missing the
`messages.dialogsNotModified#f0e3e596 count:int` variant.

## Scope
1. Responder returns `messages.dialogsNotModified` with a cached count.
2. Assert `domain_get_dialogs` returns success with zero entries and
   `total_count` set (or returns a specific sentinel the caller can
   distinguish from "truly empty").

## Acceptance
- Domain layer does not crash or misparse the not-modified variant.
- Behaviour documented in a comment next to the handler.
