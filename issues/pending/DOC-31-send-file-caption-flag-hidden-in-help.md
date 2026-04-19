# DOC-31 — send-file --caption flag hidden in help / man page

## Gap
`tg-cli send-file` and REPL `upload` both accept a caption (the
`args->message` field re-used for caption), but:

- `tg-cli --help` shows `send-file|upload <peer> <path>
  [--caption T]`.  Good — but `--caption T` is the ONLY mention.
  How does one pass a multi-word caption?  Quoting rules?  Does it
  conflict with `--message`?
- `tg-tui` REPL help says `upload <peer> <path> [caption]`.  The
  whitespace tokenizer in `do_upload` splits on the first space
  after path — a multi-word caption works only if pre-joined.  This
  is surprising and nowhere documented.
- Man pages do not include a `.TP` entry for `--caption`.

## Scope
1. Clarify `tg-cli`'s `--caption T` semantics: `T` can include spaces
   if quoted.  Add an example to `.SH EXAMPLES` in `man/tg-cli.1`:
   `tg-cli send-file @alice /tmp/doc.pdf --caption "Monthly report"`.
2. Update `tg-tui` REPL `print_help` to show `upload <peer> <path>
   [caption...]` (ellipsis indicating remainder-of-line).
3. Man pages: add a `.TP .B --caption STR` entry under the
   `send-file` / `upload` subcommand definition.

## Acceptance
- User can find caption quoting rules in help + man page.
- Man page example works copy-paste.

## Dependencies
- TEST-16 (caption propagates) already verified.
