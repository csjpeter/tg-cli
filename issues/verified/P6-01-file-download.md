# upload.getFile (file download)

## Description
Download attachments (photos, videos, documents). Must handle DC migration — files are often on a different DC.

## Batch
`tg-cli download <msg_id> [--out dir/] [--thumb]`

## Steps
1. Extract file location from Message (volume_id, local_id, etc.)
2. `upload.getFile(location, offset, limit)` — chunked download
3. Handle FILE_MIGRATE_X

## Estimate
~300 lines

## Dependencies
- P4-04 (pending) — FILE_MIGRATE_X kezelés, DC átváltás
- P5-02 (pending) — üzenet parszolás (file location kinyerése a Message-ből)
- P3-02 (pending) — bejelentkezés szükséges

## Completion notes (2026-04-16)
- Phase A: tl_skip extended — MediaInfo gained access_hash + file_reference
  (cap 128B) + dc_id + largest PhotoSize.type. photo_full walks the whole
  Photo object; walk_photo_size_vector picks the largest PhotoSize.
- Phase B: domain/read/media.{h,c} — domain_download_photo iterates
  upload.getFile in 128KB chunks, writes to out_path, surfaces
  FILE_MIGRATE_X via wrong_dc output.
- Phase C: arg_parse.c learned `download <peer> <msg_id> [--out PATH]`;
  tg-cli-ro cmd_download session-bringup → resolve peer → fetch just the
  one message (offset_id=msg_id+1, limit=1) → call domain_download_photo.
  Default out path: <cache>/downloads/photo-<id>.jpg.
- HistoryEntry gained a nested MediaInfo for the download handoff; the
  old media / media_id / media_dc summary fields still populate.
- Tests: test_domain_media.c adds 5 new cases (single chunk, RPC migrate,
  non-photo rejection, null args, empty MediaInfo). 1902 total tests
  green; valgrind clean.
- Cross-DC + Document download + CDN redirect remain TODO (tracked as
  follow-up under P4-04 / P6-02 when they land).
