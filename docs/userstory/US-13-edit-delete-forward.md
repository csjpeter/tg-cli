# US-13 — Edit / delete / forward messages

Applies to: `tg-tui`, `tg-cli`. **Never** `tg-cli-ro` (ADR-0005).

**Status:** done — shipped as P5-06.

## Story
As a user I want to fix a typo, retract a message, or re-share a message
from one chat to another from the command line or TUI.

## Scope
- `messages.editMessage` (text edits; no media re-attach in v1)
- `messages.deleteMessages` with `revoke` flag
  (server-side "delete for everyone" where allowed)
- `messages.forwardMessages` from one peer to another, preserving
  `random_id` uniqueness

## UX
```
tg-cli edit    <peer> <msg_id> <new_text>
tg-cli delete  <peer> <msg_id> [--revoke]
tg-cli forward <from_peer> <to_peer> <msg_id>
```
TUI: `edit`, `delete`/`del`, `forward`/`fwd` REPL verbs with the same arguments.

## Acceptance
- Not linkable by `tg-cli-ro` (`src/domain/write/` split).
- `MESSAGE_ID_INVALID`, `MESSAGE_AUTHOR_REQUIRED`, `PEER_ID_INVALID`
  surface with non-zero exit and a clear diagnostic to stderr.
- Channel peers resolve via `inputPeerChannel` with the correct
  `access_hash` threaded from `messages.getDialogs` (TUI-08).

## Dependencies
US-04 (peer resolution) · US-12 (shared RPC frame plumbing) · P5-06.
