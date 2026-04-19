# TEST-44 — Functional test: history --no-media across text/caption/pure-media

## Gap
`cmd_history` in `tg_cli_ro.c:642-728` has a three-way `--no-media`
decision tree:

1. Pure media (`media != NONE` and `text[0] == '\0'`) → skip.
2. Mixed (`media != NONE` and `text[0] != '\0'`) → print caption
   only, no label.
3. Plain text (`media == NONE`) → print as usual.

There is no FT exercising all three branches together.  A refactor
that, say, drops case 2 would silently hide captions that belong to
photos from `--no-media` users.

## Scope
Create `tests/functional/test_history_no_media.c`:
- Seed a history with three messages in one peer:
  - Message 1: `media=MEDIA_NONE`, text="hello".
  - Message 2: `media=MEDIA_PHOTO`, text="caption here".
  - Message 3: `media=MEDIA_DOCUMENT`, text="" (pure media).
- Run `cmd_history` with `--no-media=1` and capture stdout.
- Assert output contains exactly two lines:
  - `[… hello]`
  - `[… caption here]` WITHOUT the `[photo]` label.
- Run again without `--no-media` and assert all three messages appear.

## Acceptance
- Test pass matches the documented behaviour in `tg-cli-ro.1` for
  `--no-media`.
- JSON path also covered: `--json --no-media` suppresses the
  `media` and `media_id` fields for kept entries.
