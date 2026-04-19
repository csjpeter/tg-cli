# TEST-19 — functional test for upload NETWORK_MIGRATE_X

## Gap
US-15 scope:
> Upload path: `upload.saveFilePart` / `saveBigFilePart` →
> `NETWORK_MIGRATE_X` / `FILE_MIGRATE_X` → open target DC →
> authorize → regenerate `file_id` → retry the whole upload there.

`test_upload_download.c test_download_photo_file_migrate` covers the
download path only.

## Scope
1. Home-DC responder for `upload.saveFilePart` returns
   `rpc_error(303, "NETWORK_MIGRATE_4")`.
2. Mock server for DC 4 accepts the retried upload and returns
   `boolTrue`.
3. `messages.sendMedia` on the home DC accepts the foreign file_id.
4. Assert `mt_server_rpc_call_count_for_dc(4) == <expected_parts>`
   (requires extending `mt_server` to track per-DC counts — or seed a
   second server instance on a different mock socket).
5. Assert final `messages.sendMedia` fires once on DC 2.

## Acceptance
- Test green; covers the NETWORK_MIGRATE retry path in
  `src/domain/write/upload.c`.
