# upload.saveFilePart + messages.sendMedia (file upload)

## Batch
`tg-cli send-file <peer> file.pdf [--caption "text"]`

## Steps
1. `upload.saveFilePart(file_id, part, bytes)` — chunked upload
2. `messages.sendMedia(peer, inputMediaUploadedDocument, ...)` — send

## Estimate
~300 lines

## Dependencies
P5-01
