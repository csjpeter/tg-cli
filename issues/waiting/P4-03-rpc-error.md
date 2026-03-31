# rpc_error handling

## Description
`rpc_error` (0x2144ca19): error_code(int) + error_message(string). Cases to handle:
- `FLOOD_WAIT_X` — wait X seconds
- `PHONE_MIGRATE_X` — reconnect to DC X
- `FILE_MIGRATE_X` — file is on another DC
- `SESSION_PASSWORD_NEEDED` — 2FA required
- `AUTH_KEY_UNREGISTERED` — must re-authenticate

## Estimate
~150 lines

## Dependencies
None
