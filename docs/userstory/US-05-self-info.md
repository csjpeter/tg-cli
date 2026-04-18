# US-05 — Show own profile (self info)

Applies to: `tg-cli-ro`, `tg-tui`.

**Status:** done — `users.getUsers([inputUserSelf])` wired end-to-end
(batch `me` + REPL `me`).

## Story
As an authenticated user I want to confirm which account I am logged in as
so that I know my session is still valid and on the right identity.

## Scope
`users.getUsers([inputUserSelf])` → id, first_name, last_name, username,
phone, language, premium flag.

## UX
```
tg-cli-ro me [--json]
tg-cli-ro self           # alias
```
Default output:
```
id:       123456789
username: @example
name:     Example User
phone:    +15551234567
premium:  no
```
`--json` emits a single object.

## Acceptance
- Works right after login (auth_key already loaded).
- Returns non-zero exit on RPC error or unauthenticated state.
- Zero mutations on server (read-only guarantee).

## Dependencies
US-03 auth · F-02. This is the **first vertical slice** that proves the
full read-only pipeline.
