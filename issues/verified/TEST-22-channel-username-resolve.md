# TEST-22 — functional test for channel `@username` resolve

## Gap
US-16 scope:
> `contacts.resolveUsername` → `(peer_id, access_hash)` for both DMs
> **and channels**.

`test_resolve_username_happy` returns a user; there is no functional
test for the channel resolution path (`TL_channel` inside
`contacts.resolvedPeer.chats`).

## Scope
1. Responder returns `contacts.resolvedPeer` with `peer =
   peerChannel(id=...)` and a `chats` vector containing `TL_channel`
   with `access_hash`.
2. Assert `domain_resolve_username` threads the `access_hash` out.
3. Follow-up call (`messages.getHistory` with
   `inputPeerChannel(channel_id, access_hash)`) carries the correct
   access_hash on the wire.

## Acceptance
- Test green; channel branch covered in
  `src/domain/read/user_info.c` (or wherever resolve lives).
