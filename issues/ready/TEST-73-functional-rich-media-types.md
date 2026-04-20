# TEST-73 — Functional coverage for rich media types (video/audio/voice/sticker/animation/geo/contact/poll/webpage)

## Gap
`src/domain/read/media.c` is 63 % functionally covered. Only photo
and plain document paths are exercised. Nine other media kinds are
silently dropped or labelled wrongly.

## Scope
Mock-server extension: `mt_server_seed_media_reply(kind, …)` builders
for each of the nine kinds from US-22.

New test suite `tests/functional/test_media_types.c`:

| test | feeds … | asserts |
|---|---|---|
| `test_history_video_label` | `messageMediaDocument` + `documentAttributeVideo` | label "[video WxH Ds BYTES]" |
| `test_history_audio_label` | audio attr, voice=false | "[audio Ms mime]" |
| `test_history_voice_label` | audio attr, voice=true | "[voice Ms ogg]" |
| `test_history_sticker_label` | sticker attr | "[sticker :alias: pack 'name']" |
| `test_history_animation_label` | animated attr | "[gif WxH Ds]" |
| `test_history_round_video_label` | video attr round=true | "[round-video Ds]" |
| `test_history_geo_label` | `messageMediaGeo` | "[geo LAT,LON]" |
| `test_history_contact_label` | `messageMediaContact` | "[contact PHONE NAME]" |
| `test_history_poll_label` | `messageMediaPoll` | "[poll 'question' V/N votes]" |
| `test_history_webpage_label` | `messageMediaWebPage` | "[link example.com …]" |
| `test_download_voice_extension` | voice note | saved as `.ogg` |
| `test_download_sticker_extension` | sticker | saved as `.webp` |
| `test_download_video_extension` | video | saved as `.mp4` |

## Acceptance
- 13 tests pass under ASAN.
- Functional coverage of `media.c` ≥ 85 %.
- Updated media labels match the man-page tables for tg-cli and
  tg-cli-ro.

## Dependencies
US-22 (the story). Baseline US-08 (download). FEAT-26 (upload-side
attrs) is parallel.
