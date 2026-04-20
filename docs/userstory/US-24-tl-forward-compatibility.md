# US-24 — TL forward compatibility: survive unknown server types

Applies to: all three binaries.

**Status:** gap — `src/core/tl_skip.c` is 13.6 % functional covered
(1717 / 1988 lines uncovered). The skip table is populated from the
generated schema, but almost none of its branches fire in the
functional suite because the mock server only returns the handful of
types we explicitly handle.

## Story
As a user running a tg-cli binary built against TL layer N, I want
Telegram's layer N+1 rollouts to degrade gracefully — an unknown
result type or an unknown optional field inside a known type must
cause at worst a "unknown field, ignored" log line, never a crash,
parse abort, or connection drop.

## Uncovered practical paths
- **Unknown top-level result:** RPC returns a `CRC` that tl_skip does
  not know either → skip by reading bytes using the TL rules
  (`vector`, `int256`, `bytes`, nested object) and continue.
- **New optional field in a known type:** message flags grow a new
  bit (e.g. `messageMedia` with a new entry); we must not misread
  the remaining fields.
- **New variant of a known constructor family:** e.g. a new
  `messageAction*` CRC inside `history` → label "[unknown action]"
  but keep the surrounding message's text and id.
- **Schema upgrade at runtime:** server replies with
  `updates.too_long` or returns a newer `layer` in `config.config` →
  we log and continue.

## Acceptance
- Mock server seeds a deliberately-unknown CRC inside a known
  envelope and the functional test proves the rest of the response
  is parsed correctly.
- Three new tests in `tests/functional/test_tl_forward_compat.c`:
  1. unknown top-level result in `messages.getDialogs` response →
     dialog list still populated for the known entries.
  2. unknown media type in `history` → text field still printed,
     media labelled "[unknown media]".
  3. unknown `messageAction` in a service message → message id +
     date still listed.
- Functional coverage of `tl_skip.c` ≥ 40 % (from 13.6 %). Not
  aiming at 100 % because of the generator-emitted long tail.
- `docs/dev/mtproto-reference.md` gets a "forward-compat guarantees"
  paragraph.

## Dependencies
Core TL serializer work is done (P-series). No new infra needed.
