# auth.sendCode + auth.signIn

## Description
Login flow: phone number → SMS code → session.

## API
- `auth.sendCode` — send phone number, request code
- `auth.signIn` — send received code back
- `auth.signUp` — if new user (unlikely)

## Steps
1. `auth.sendCode(phone, api_id, api_hash)` → `auth.sentCode`
2. User enters code (stdin / interactive)
3. `auth.signIn(phone, phone_code_hash, code)` → `auth.authorization`

## Estimate
~300 lines

## Dependencies
- P3-01 ✅ (verified) — initConnection wrapper
- P2-auth-key-exchange ✅ (ready) — titkosított csatorna szükséges
- P2-session ✅ (ready) — msg_id/seq_no kezelés

Nincs pending függőség — önállóan indítható.

## Reviewed — 2026-04-16
Pass. Confirmed auth_send_code + auth_sign_in in src/infrastructure/auth_session.{c,h} (266 LOC module + 468 LOC tests). TL constructor IDs for auth.sendCode/signIn/codeSettings/authorization defined. Proper RpcError propagation, phone_code_hash threaded through. Commit 3644cd2 logged.

## QA — 2026-04-16
Pass. auth_send_code + auth_sign_in in src/infrastructure/auth_session.c, with tests/unit/test_auth_session.c (24 tests, 468 LOC). RpcError propagation tested; phone_code_hash threading verified. 1735/1735 green; Valgrind 0 errors. auth_session.c hits 86% line coverage — remaining gap is defensive NULL handling.
