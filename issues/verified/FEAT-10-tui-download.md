# FEAT-10 — `download` REPL verb in tg-tui

## Gap
US-08 + US-11 imply the TUI re-exposes every read surface of
`tg-cli-ro`, including media download. The REPL today lists no
`download` command, and the pane-mode viewer cannot open attached
photos.

## Scope
1. REPL verb: `download <peer> <msg_id> [out]` calling the same
   `domain_download_media_cross_dc` used by tg-cli-ro's `cmd_download`.
2. Default output path mirrors tg-cli-ro
   (`~/.cache/tg-cli/downloads/photo-<photo_id>.jpg`).
3. Pane-mode integration (stretch goal): opening a dialog lists
   downloadable attachments with `d <n>` to pull one. Out of scope if
   it bloats TUI too much — ticket then only delivers the REPL verb.

## Acceptance
- `tg> download @peer 12345 ~/pic.jpg` pulls the photo into the given
  path; success + error reported inline in the REPL.
- `print_help` + `man/tg-tui.1` updated (DOC-03).
