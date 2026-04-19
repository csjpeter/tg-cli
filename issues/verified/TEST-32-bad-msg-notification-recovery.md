# TEST-32: bad_msg_notification handling returns SVC_ERROR but is never tested

## Location
`src/infrastructure/api_call.c` – `classify_service_frame()`, line ~96–104
`tests/unit/test_api_call.c`

## Problem
`bad_msg_notification` (CRC 0xa7eff811) is handled by returning `SVC_ERROR`
with a `LOG_WARN`.  This means any msg_id / seqno disagreement terminates the
call.  However there are no unit tests exercising this path.  Specifically
missing:

1. A test that feeds a `bad_msg_notification` frame as the first response to
   `api_call()` and asserts the call returns -1.
2. A test that feeds `bad_msg_notification` *after* a valid service frame (e.g.
   `msgs_ack`) to verify the loop correctly bails after draining.
3. A test for the `resp_len < 28` guard that extracts `error_code`.

The `bad_server_salt` retry path has two tests (`test_bad_server_salt_retry`,
`test_new_session_created_skipped`); the `bad_msg_notification` path has zero.

## Add to
`tests/unit/test_api_call.c`
