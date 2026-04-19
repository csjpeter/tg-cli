# TEST-18 — functional test for UPLOAD_MAX_SIZE enforcement

## Gap
US-14 scope:
> capped at `UPLOAD_MAX_SIZE = 1.5 GiB`

No test verifies the cap. A regression could let us attempt a 4 GiB
upload and then fail mid-stream in production.

## Scope
Because a real 1.5 GiB fixture is impractical in CI, fake the size:
1. Add a test-only shim (`domain_send_file_with_size_override`) or
   inject a stat-mock, so `domain_send_file` "sees" a 2 GiB file
   against a tiny actual fixture.
2. Assert the call returns non-zero immediately with a clear
   "file too large" diagnostic; no part is uploaded.

## Acceptance
- Test green; no wire RPC fires; error string asserted.
