# MTProto Session
src/core/mtproto_session.h/c

## QA Reject — 2026-03-31

### No RAII_FILE for file handles
- `mtproto_session_save_auth_key` (line 66) and `mtproto_session_load_auth_key`
  (line 75) use raw `fopen`/`fclose` instead of `RAII_FILE`

### Auth key saved without restrictive permissions
- `fopen(path, "wb")` creates file with default permissions (0644)
- Auth key is sensitive crypto material — should use mode 0600
  (consistent with config.ini handling)

### Incomplete Doxygen
- `mtproto_session_init`, `mtproto_session_next_msg_id`,
  `mtproto_session_set_auth_key`, `mtproto_session_save_auth_key`,
  `mtproto_session_load_auth_key` lack `@param` and `@return` tags

### Missing failure-path tests
- No test for `load_auth_key` with non-existent file
- No test for `load_auth_key` with truncated file
- No test for `save_auth_key` with invalid path

## Reviewed — 2026-04-16
Pass. Confirmed RAII_FILE on fopen in save_auth_key (line 104) and load_auth_key (line 125). fs_ensure_permissions(path, 0600) called after open. All public functions have Doxygen. tests/unit/test_phase2.c covers missing-file, truncated-file, invalid-path, NULL-args edge cases (lines 386-432).
