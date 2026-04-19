# TEST-07 — functional test for document download

## Gap
`test_upload_download.c` covers photo download (single chunk, two
chunks, FILE_MIGRATE). Document download has no functional coverage,
despite being on the user-visible surface (US-08 + future FEAT-06).

## Scope
1. Responder for `upload.getFile` returns a multi-chunk document
   (e.g. 1 MiB of deterministic bytes).
2. Caller is `domain_download_document` (or `domain_download_media_cross_dc`
   with a document MediaInfo).
3. Assert the chunk concatenation matches the expected SHA-256.
4. Cover the "filename from DocumentAttributeFilename" path.

## Acceptance
- New test green; covers `src/domain/read/media.c` document branch.
