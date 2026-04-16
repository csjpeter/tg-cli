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
