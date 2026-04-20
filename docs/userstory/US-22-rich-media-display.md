# US-22 — Rich media display: video, audio, voice, sticker, animation

Applies to: tg-cli, tg-cli-ro (tg-tui inherits).

**Status:** gap — `src/domain/read/media.c` is 63 % functional covered.
Photo and plain document paths are exercised. Video, audio, voice,
sticker, animation (GIF), round-video, and web-document media return
stubs or bypass parsing.

## Story
As a user reading history from a channel or group that sends more
than plain text, I want each message in `history` output to say what
kind of media it carries, its duration / size / sticker pack where
relevant, and (for `download`) save the real file with a sensible
name and extension.

## Concrete gaps (from current wire handling)
| media type | current behaviour | desired |
|---|---|---|
| `messageMediaVideo` | labelled "[document]" | "[video 1280x720 42s 1.2MB]" |
| `messageMediaAudio` / documentAttributeAudio voice=false | not parsed | "[audio 3m17s mp3]" |
| documentAttributeAudio voice=true | not parsed | "[voice 0m08s ogg]" |
| documentAttributeSticker | not parsed | "[sticker :heart_eyes: pack 'Anim.2']" |
| documentAttributeAnimated | not parsed | "[gif 480x480 3.0s]" |
| documentAttributeVideo round=true | not parsed | "[round-video 0m04s]" |
| `messageMediaGeo` | silently dropped | "[geo 47.4979,19.0402]" |
| `messageMediaContact` | silently dropped | "[contact +36... János]" |
| `messageMediaPoll` | silently dropped | "[poll 'Sunny?' 3/14 votes]" |
| `messageMediaWebPage` | silently dropped | "[link example.com …]" |

## Uncovered practical paths
- **`download` naming:** extension inferred from mime-type, fallback
  to `documentAttributeFilename`. Voice notes become `.ogg`, stickers
  `.webp`, round video `.mp4`.
- **Unknown attribute:** forward compatibility — unknown attr CRC is
  skipped via `tl_skip` without crashing.
- **FEAT-10 media index:** each of the above should generate an
  index entry so `history` rehydrates a local path if the file was
  previously downloaded.

## Acceptance
- New functional tests in `tests/functional/test_media_types.c` seed
  each of the nine media variants in mock server responses and
  assert the printed label and (for `download`) the saved extension.
- Functional coverage of `media.c` ≥ 85 % (from 63 %).
- `tg-cli-ro history` and `tg-tui history` output updated to match.
- Man pages get a compact "Supported media kinds" table.

## Dependencies
FEAT-26 (upload-side video/audio attrs) is the mirror on the write
path. US-08 (media download) remains the baseline.
