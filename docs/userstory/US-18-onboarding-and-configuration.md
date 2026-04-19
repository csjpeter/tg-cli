# US-18 — Onboarding and configuration

Applies to: `tg-tui`, `tg-cli`, `tg-cli-ro` (shared wizard + doc).

**Status:** pending — blocked on FEAT-37 (wizard) and DOC-34 (guide).

## Story

As a new user, I need to go from a fresh git clone to a working authenticated
tg-cli session in under 10 minutes, with interactive help at every step where
my input is ambiguous or incorrect.

## Acceptance

- **Wizard exists (FEAT-37):** `tg-tui login` (and `tg-cli login`,
  `tg-cli-ro login`) runs an interactive wizard that prompts for api_id
  (echoed) and api_hash (masked), validates both, and writes
  `~/.config/tg-cli/config.ini` with mode 0600.
- **Doc exists (DOC-34):** `docs/user/setup-my-telegram-org.md` contains a
  step-by-step guide from my.telegram.org login through credential
  verification with `tg-cli-ro me`.
- **README quickstart is a 5-liner:** The Prerequisites section in `README.md`
  is a short pointer to the guide and the wizard — no inline instructions.
- **Man pages have CONFIGURATION section:** All three man pages
  (`tg-cli.1`, `tg-cli-ro.1`, `tg-tui.1`) include a `.SH CONFIGURATION`
  section explaining the three credential-supply options and pointing to
  `docs/user/setup-my-telegram-org.md`.
- **api_hash entry is masked:** The wizard does not echo the api_hash to the
  terminal during entry.
- **Overwriting existing config requires `--force`:** Running the wizard when
  `~/.config/tg-cli/config.ini` is already non-empty exits with an error
  unless `--force` is passed.
- **Batch flags work:** `--batch --api-id N --api-hash H` writes the config
  non-interactively (for CI / scripted setup), provided stdin is not a TTY.

## Out of scope

- OAuth flows.
- Device authorization / QR-code login.
- Bot token authentication.

## Dependencies

- FEAT-37 — first-run interactive config wizard (implements the `login`
  subcommand referenced above).
- DOC-34 — my.telegram.org setup guide (provides the step-by-step doc
  referenced by the wizard and man pages).
