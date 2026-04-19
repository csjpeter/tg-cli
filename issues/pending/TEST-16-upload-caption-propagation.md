# TEST-16 — functional test for `--caption` propagation

## Gap
US-14 UX:
> `tg-cli send-file <peer> <path> [--caption TEXT]`

`test_upload_download.c` covers file-part layout but does not assert
the caption actually lands on the wire as the `message` field of
`messages.sendMedia`.

## Scope
1. Upload a small file with `--caption "final version"`.
2. `messages.sendMedia` responder inspects the TL body, reads the
   `message` string, and asserts it equals "final version".
3. Second case: caption omitted → empty string on the wire.

## Acceptance
- Two tests green; caption propagation covered.
