# US-03 — First-time user login works end-to-end

Applies to: all three binaries (shared by `src/app/auth.c`).

**Status:** done — `auth.sendCode`, `auth.signIn`, PHONE_MIGRATE_X
rerouting, `account.getPassword` + SRP + `auth.checkPassword` for 2FA,
session persistence and `--logout` all shipped (P3-02, P3-03, P4-04).

## Story
As a user running tg-cli-ro or tg-tui for the first time, I want to
authenticate with my phone number and the received code so that I can access
my account.

## Flow
1. Load api_id/api_hash from `~/.config/tg-cli/config.ini`.
2. Connect to DC2 (default) and perform DH auth-key exchange (done).
3. Prompt for phone number → `auth.sendCode` (done, P3-02).
4. Prompt for code (interactive) or read from `--code STR` (batch).
5. `auth.signIn` (done, P3-02).
6. On `PHONE_MIGRATE_X`: reconnect to DC X, re-run DH, retry sign-in (P4-04).
7. On `SESSION_PASSWORD_NEEDED`: SRP 2FA password (P3-03).
8. Persist auth key under `~/.config/tg-cli/auth.key` (mode 0600).

## Batch mode (tg-cli-ro)
```
tg-cli-ro --batch --phone +15551234567 --code 12345 [--password PASS] me
```
If a secret is not provided, the binary exits non-zero with a clear
"needs credentials" message. Never reads `stdin` in `--batch`.

## Acceptance
- On first run the key is created; subsequent runs load it and skip login.
- 2FA is requested only when the server asks for it.
- PHONE_MIGRATE_X is handled transparently.
- `auth.key` file mode is 0600.

## Dependencies
P3-02 (done) · P3-03 · P4-04 · QA-22.
