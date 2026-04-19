# FEAT-12 — call `auth.logOut` on `--logout` before wiping session

## Gap
US-16 acceptance:
> `--logout` flag wipes the session file **and invalidates the key
> with `auth.logOut` on the server** before exit.

Current code (`tg_cli_ro.c:647`, `tg_cli.c:350`, `tg_tui.c` logout
shortcut) just calls `session_store_clear()` — the server is not told
the session is invalid. The key remains valid on Telegram's side until
it naturally expires, which defeats the "I want a clean logout"
contract.

## Scope
1. New `domain_auth_logout()` in `src/domain/write/` wrapping
   `auth.logOut#3e72ba19` (returns `auth.loggedOut`).
2. Before `session_store_clear()`, open transport → load session →
   call `domain_auth_logout` → ignore a "not authorized" error
   (session already dead). Only then wipe the file.
3. Functional test: register a responder for `auth.logOut`, run the
   `--logout` path, assert the RPC fired, then assert the session file
   is gone.
4. Document in tg-cli-ro(1) / tg-cli(1) / tg-tui(1) + US-16.

## Acceptance
- After `tg-cli-ro --logout`, the server-side key is invalid (in the
  functional harness: responder saw the `auth.logOut` CRC).
- `session.bin` removed.
- A tailing `auth.logOut` network failure does not prevent local
  wipe — we still clear the file so the user's "please forget me"
  intent is honoured.
