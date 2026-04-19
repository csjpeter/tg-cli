# FEAT-06 — `download` supports documents, not just photos

## Gap
US-08 status note says documents download is shipped "for the common
plain document case". The CLI surface (`tg-cli-ro download <peer>
<msg_id>`) currently only wires the photo path. Documents referenced
in messages cannot be pulled by msg_id today.

## Scope
1. In `tg_cli_ro.c cmd_download`, dispatch on the extracted media kind
   from `tl_skip_message_media_ex`:
   - photo → current code path.
   - document → use the document's `(id, access_hash, file_reference,
     dc_id)` to drive `upload.getFile` via `domain_download_document`.
2. Default output filename: prefer the `DocumentAttributeFilename` if
   present, otherwise `document-<id>.bin`.
3. Unit test for the dispatch; functional test for a canned
   `messageMediaDocument` download.

## Acceptance
- `tg-cli-ro download @peer 12345 --out foo.pdf` works for a PDF
  document attachment.
- Sticker / customEmoji variants still fall through the existing
  complex-skip bail (known limitation, US-00 backlog).
