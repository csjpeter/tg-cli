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
P4-04 (DC migration), P5-02 (message parse)
