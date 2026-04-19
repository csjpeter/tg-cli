# DOC-29 — README: add quickstart and per-binary example blocks

## Gap
The project's `README.md` documents architecture but not:

1. "Install → authenticate → send first message in 3 lines" quickstart.
2. Per-binary examples side-by-side:
   - `tg-cli-ro dialogs --limit 5 --json | jq`
   - `tg-cli send @alice --reply 42 "reply here"`
   - `tg-tui --tui` (screenshot or ASCII)
3. Ubuntu / Rocky / macOS / Windows install + run instructions.
4. FAQ entries for common errors (no config.ini, wrong api_id, DC
   unreachable, 2FA password prompt hang).
5. Cross-link to user stories (docs/userstory/) as a feature map.

## Scope
1. Add `## Quickstart` section at top with 5-line shell snippet:
   ```
   git clone … && cd tg-cli
   ./manage.sh build
   echo "api_id=…" > ~/.config/tg-cli/config.ini
   ./bin/tg-cli-ro --phone +15551234 --code 12345 me
   ```
2. Add `## Examples` with 6-8 worked invocations.
3. Add `## FAQ` with 6-8 entries.

## Acceptance
- README teaches a new user to install + run in under 5 minutes.
- Each example is copy-pasteable and verified against current
  binary.

## Dependencies
- None — pure documentation.
