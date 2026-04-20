# US-33 — Config INI robustness: comments, CRLF, BOM, partial keys

Applies to: all three binaries.

**Status:** gap — `src/app/credentials.c` is 56 % functional
covered. TEST-57 tests mode enforcement. Lexical / parser edge
cases for `config.ini` (comments, CRLF line endings, UTF-8 BOM,
whitespace, quoted values, missing halves of the credential pair)
have no functional test.

## Story
As a user handing my `~/.config/tg-cli/config.ini` to a colleague
on Windows, or editing it in Notepad with a Byte-Order Mark and
CRLF line endings, I want tg-cli to load my credentials exactly
the same way it does from the canonical mode-0600 LF-only file
emitted by the wizard — no mystery "credentials not found" errors
because of an invisible CR.

## Uncovered practical paths
- **CRLF line endings** on every line → api_id / api_hash parsed
  correctly (no CR left in the api_hash string).
- **UTF-8 BOM** at file start → skipped, parser reads `api_id=…`
  on the first line.
- **Leading / trailing whitespace** around key and value.
- **Comments starting with `#` or `;`** → ignored.
- **Quoted values** (`api_hash="dead..."`) → quotes stripped.
- **Empty value** → reports missing credentials, does not
  segfault.
- **Only api_id, no api_hash** → explicit "api_hash missing" error
  referencing the wizard.
- **Only api_hash, no api_id** → symmetric.
- **Duplicate keys** → last occurrence wins; log a warning.
- **File exists but empty** → same diagnostic as "file missing".

## Acceptance
- New suite `tests/functional/test_config_ini_robustness.c` with
  one test per case above, each seeding a byte-level `config.ini`
  and asserting either successful parse with the expected values
  OR a specific diagnostic on stderr.
- Functional coverage of `credentials.c` ≥ 90 % (from 56 %).
- Man page CONFIGURATION section for all three binaries gains a
  "Accepted variations" paragraph.

## Dependencies
US-18 (onboarding / wizard writes the canonical file).
TEST-57 (mode enforcement) stays orthogonal.
