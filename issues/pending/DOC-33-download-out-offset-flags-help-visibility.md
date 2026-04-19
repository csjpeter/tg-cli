# DOC-33 — --out and --offset flags need clearer help-text visibility

## Gap
Both flags are implemented and tested, but:

- `--out <path>` for `download` appears only once in the help line
  `download <peer> <msg_id> [--out PATH]`.  No explanation of what
  the default path is when `--out` is omitted (it's
  `$XDG_CACHE_HOME/tg-cli/downloads/<filename-or-photo-id>.jpg`).
- `--offset N` for `history` similarly is bare.  No mention of what
  the offset refers to: message id, not row index.

New users get confused trying to paginate `history` with `--offset 20`
expecting row 20, but it actually means "start before message id 20".

## Scope
1. In `print_usage` for `tg-cli-ro`:
   - Expand `history` line with a brief "--offset MSG_ID" note.
   - Expand `download` line with the default path.
2. In `man/tg-cli-ro.1` `.SH SUBCOMMANDS`, add `.TP` entries:
   - `.B --offset MSG_ID` — "Start history before this message id.
     Use the id from a previous `history` row to scroll back.  0
     means the most recent messages (default)."
   - `.B --out PATH` — "Destination path for downloaded media.
     Default: `$XDG_CACHE_HOME/tg-cli/downloads/<name>`."
3. Add one worked `.SH EXAMPLES` entry showing pagination:
   ```
   tg-cli-ro history @alice --limit 10
   # returns ids 100..91; to get the next page:
   tg-cli-ro history @alice --limit 10 --offset 91
   ```

## Acceptance
- Users learn pagination semantics from help + man page alone.
- `--out` default path is discoverable without reading source.

## Dependencies
- None.
