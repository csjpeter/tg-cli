# DC Migration

## Description
Telegram operates 5 DCs. During login and file downloads, the server may redirect to a different DC. Requires: DC list, reconnection, auth key export/import.

## API
- `help.getConfig` → DC list (dc_id, ip, port)
- `auth.exportAuthorization(dc_id)` → bytes
- `auth.importAuthorization(id, bytes)` on target DC

## Estimate
~200 lines

## Dependencies
- P4-03 ✅ (verified) — rpc_error PHONE_MIGRATE_X / FILE_MIGRATE_X detektálás
- P3-02 (pending) — auth.exportAuthorization/importAuthorization bejelentkezést igényel
