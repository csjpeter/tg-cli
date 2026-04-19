# TEST-20 — functional test for upload FILE_MIGRATE_X

## Gap
Like TEST-19 but for `FILE_MIGRATE_X` on the upload path, which is
the other error code Telegram may return when the target DC is
assigned per-file instead of per-network.

## Scope
1. Home-DC responder for `upload.saveFilePart` returns
   `rpc_error(303, "FILE_MIGRATE_3")`.
2. Retried upload lands on DC 3; final `messages.sendMedia` on the
   home DC.

## Acceptance
- Test green; covers `FILE_MIGRATE` distinct from `NETWORK_MIGRATE`
  in the upload retry loop.
