# TEST-69 — Functional test: RTL (Arabic/Hebrew) text does not break terminal state

## Gap
`screen_put_str_n` (per SEC-01 verified) sanitizes control bytes,
but does not special-case Unicode bidirectional control characters
(U+202A..U+202E: LRE, RLE, PDF, LRO, RLO — all printable, width 0,
but flip terminal display direction permanently).  A message
containing a raw LRO followed by newlines could leave subsequent
output rendered right-to-left until the next RIS.

## Scope
Create `tests/functional/test_rtl_bidi_text.c`:
- Seed a history message with `"Hello \u202Eevil\u202C"` (RLO + PDF).
- Paint into the history pane.
- Assert the painted bytes include a Unicode isolation Mark
  (U+2068..U+2069 FSI/PDI) inserted by the painter OR the raw LRO
  is replaced with its visible escape (`<RLO>`).
- Sibling: plain RTL text (Arabic letters) passes through unchanged.

Related: SEC-01 may or may not consider bidi controls as threats —
define the contract here (proposed: wrap user text in FSI…PDI so
bidi overrides don't leak).

## Acceptance
- Malicious RLO in message text does not affect the painting of
  subsequent rows.
- Regular RTL text (no overrides) renders as-is.
- If the decision is to strip U+202A..U+202E, document in SECURITY.

## Dependencies
- SEC-01 (verified) — extend, not replace.
