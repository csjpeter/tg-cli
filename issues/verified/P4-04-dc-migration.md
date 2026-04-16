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

## Verified — 2026-04-16 (v1)
- `src/app/auth_flow.c` handles PHONE_MIGRATE_X / USER_MIGRATE_X /
  NETWORK_MIGRATE_X by tearing down the transport + session and
  reconnecting to the target DC via `auth_flow_connect_dc()`.
- `src/app/dc_config.c` ships a hardcoded DC endpoint table (1..5).
- `auth.exportAuthorization` / `importAuthorization` **not** used —
  only the simpler reconnect-then-reauth path. Remains TODO if the
  user has media across multiple DCs.
