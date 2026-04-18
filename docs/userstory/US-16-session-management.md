# US-16 — Resolve `@username` + persistent session + `--logout`

Applies to: all three binaries.

**Status:** done — shipped as P7-03 + session persistence + `--logout`.

## Story
As a user I want to refer to chats by `@username` (not numeric id), I
want to stay logged in across invocations, and I want a clear way to
wipe my session when I'm done.

## Scope
- `contacts.resolveUsername` → `(peer_id, access_hash)` for both DMs
  and channels
- Persist `auth_key` + `dc_id` + `server_salt` + home DC session ids
  under `~/.config/tg-cli/session.bin` (mode 0600)
- `--logout` flag wipes the session file and invalidates the key with
  `auth.logOut` on the server before exit

## UX
```
tg-cli-ro history @channel --limit 20       # resolves @channel
tg-cli-ro --logout                           # invalidate + wipe
```

## Acceptance
- `@name` accepted everywhere a peer is expected; numeric id and
  `self` still work.
- Second invocation within the session TTL skips login entirely (no
  SMS, no 2FA).
- After `--logout`, the next invocation starts a fresh login flow.

## Dependencies
US-03 (login bootstrap) · P7-03 · session store.
