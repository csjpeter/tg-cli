# US-09 — Contacts and user-info lookups

Applies to: `tg-cli-ro`, `tg-tui`.

## Story
As a user I want to resolve a username to a user/channel and look up its
public info so that I can confirm who/what I am talking to.

## Scope
- `tg-cli-ro contacts` → `contacts.getContacts` (F-07).
- `tg-cli-ro user-info <peer>` → `contacts.resolveUsername` +
  `users.getFullUser` (F-06).

## UX
Plain output: labelled block. JSON: single object.

## Acceptance
- `user-info` supports `@username`, numeric id, and `self`.
- RPC errors surface as exit code ≠ 0 with a diagnostic to stderr.

## Dependencies
US-03 · P7-01 / P7-02 / P7-03.
