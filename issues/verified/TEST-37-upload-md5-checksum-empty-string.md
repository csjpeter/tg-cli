# TEST-37: upload.saveFilePart: md5_checksum sent as empty string, server rejection untested

## Location
`src/domain/write/upload.c` – `write_input_file()`

## Problem
For small files (`InputFile`), the TL serialization passes `""` for the
`md5_checksum` field:
```c
tl_write_string(w, "");   /* md5_checksum */
```

Telegram's server **accepts** an empty checksum for `saveFilePart` (the field is
advisory), but:
1. There is no test confirming that the server round-trip works with empty MD5.
2. There is no test for what happens when the real Telegram server returns
   `PHOTO_INVALID_DIMENSIONS` or `FILE_PARTS_INVALID` (upload phase errors) —
   the functional test `tests/functional/test_upload_download.c` only tests the
   happy path.
3. The `InputFileBig` constructor omits the `md5_checksum` field entirely (by
   spec) — confirmed correct in `write_input_file()` — but there is no test
   asserting the `InputFileBig` wire format does NOT include a checksum field.

## Add to
`tests/unit/test_domain_upload.c`:
- Test that `inputFile` wire bytes contain exactly one empty TL string after the
  part count.
- Test that `inputFileBig` wire bytes contain NO empty string after the part count.
