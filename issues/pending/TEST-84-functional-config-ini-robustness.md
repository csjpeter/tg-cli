# TEST-84 — Functional coverage for config.ini parser edge cases

## Gap
`src/app/credentials.c` is 56 % functional covered. Mode
enforcement is tested (TEST-57). Lexical variants — CRLF, BOM,
comments, quoted values, whitespace, partial creds — have no
functional test.

## Scope
New suite `tests/functional/test_config_ini_robustness.c` — one
test per case:

1. `test_crlf_line_endings_parsed_cleanly` — file written with
   `\r\n`; api_id/hash parsed without trailing `\r`.
2. `test_utf8_bom_skipped_at_start` — file begins with EF BB BF;
   first line's key recognised.
3. `test_hash_comment_ignored` — `# tg-cli config` as the first
   line; parse continues.
4. `test_semicolon_comment_ignored` — `; alt-comment` form.
5. `test_leading_trailing_whitespace_trimmed` — `  api_id =  12345  `.
6. `test_quoted_value_strips_quotes` — `api_hash="dead…"`.
7. `test_empty_value_is_missing_credential` — `api_id=\n`; clear
   diagnostic rather than a crash.
8. `test_only_api_id_reports_api_hash_missing` — explicit wording
   pointing at the wizard.
9. `test_only_api_hash_reports_api_id_missing`.
10. `test_duplicate_key_last_wins_and_warns` — `api_id` listed
    twice; value from the second line wins, logger records
    LOG_WARN.
11. `test_empty_file_is_missing_credentials` — zero-byte file.
12. `test_api_hash_wrong_length_rejected` — hash with 31 or 33
    chars.

## Acceptance
- 12 tests pass under ASAN.
- Functional coverage of `credentials.c` ≥ 90 % (from 56 %).

## Dependencies
US-33 (the story). TEST-57 (mode enforcement) stays orthogonal.
