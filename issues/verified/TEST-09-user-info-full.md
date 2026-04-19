# TEST-09 — functional test for users.getFullUser

## Gap
US-09 scope:
> `user-info <peer>` → `contacts.resolveUsername` +
> `users.getFullUser` (F-06).

`test_read_path.c test_resolve_username_*` covers the resolve step
only. The `users.getFullUser#b9f11a99` follow-up call and its reply
(`userFull#cc997720`) have no functional coverage.

## Scope
1. Responder for `contacts.resolveUsername` returns a
   `contacts.resolvedPeer`.
2. Responder for `users.getFullUser` returns a canned
   `userFull` with `about`, `common_chats_count`, etc.
3. Assert `domain_get_user_info` walks both calls and surfaces the
   extracted fields.

## Acceptance
- New test green; covers the `getFullUser` branch in
  `src/domain/read/user_info.c`.
