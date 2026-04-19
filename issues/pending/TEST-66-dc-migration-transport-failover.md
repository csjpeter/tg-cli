# TEST-66 — Functional test: DC migration preserves in-flight work

## Gap
P4-04 (verified) implemented DC migration on `PHONE_MIGRATE_X`,
`NETWORK_MIGRATE_X`, `USER_MIGRATE_X`.  `test_upload_download.c`
covers upload-side migration (`FILE_MIGRATE_X` during `saveFilePart`)
and media download migration (`upload.getFile`).  Nothing covers:

1. `auth.signIn` returning `PHONE_MIGRATE_2` mid-flow → the client
   should reconnect to DC2, re-seed `auth.sendCode` if needed, and
   complete.
2. `USER_MIGRATE_X` on `messages.sendMessage` → the client moves to
   the new DC and retries the send.
3. Three-way migration: initial DC1 → FILE_MIGRATE → DC2 →
   NETWORK_MIGRATE → DC3 (handshake + retry chain).

## Scope
Create `tests/functional/test_dc_migration_chains.c`:
- Use `mt_server_arm_reconnect` to script:
  - Case 1: login DC1 → PHONE_MIGRATE 2 → DC2 handshake → auth.signIn
    succeeds.
  - Case 2: sendMessage DC2 → USER_MIGRATE 4 → DC4 handshake →
    sendMessage succeeds.
  - Case 3: upload DC2 → FILE_MIGRATE 3 → partial → NETWORK_MIGRATE 5
    → DC5 resumes upload.
- Assert final state, RPC counts on each DC, zero data loss.

## Acceptance
- 3 scenarios pass under ASAN.
- Each migration preserves user-visible outcome (message sent, file
  uploaded, history fetched).

## Dependencies
- P4-04 (verified).
- Mock extensions from TEST-19 / TEST-20 reusable.
