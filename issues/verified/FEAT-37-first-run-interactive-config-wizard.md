# FEAT-37 — First-run interactive config wizard (api_id / api_hash setup)

## Gap
When a user runs `bin/tg-tui` (or any binary) without a pre-existing
`~/.config/tg-cli/config.ini` and without `TG_CLI_API_ID` /
`TG_CLI_API_HASH` env vars, they get a terse stderr line:

```
ERROR: credentials: api_id/api_hash not found. Set TG_CLI_API_ID and
TG_CLI_API_HASH env vars, or add api_id=/api_hash= lines to
~/.config/tg-cli/config.ini
```

No hand-holding for the new user:
- Where do I get an api_id/api_hash?
- What format is the config file?
- What permissions does it need?
- Can I just paste them here and have the binary save it?

All other CLI tools with API-key requirements (aws, gh, docker login)
ship an interactive configurator for the first run.  tg-cli needs one.

## Scope

### 1. New subcommand / entry mode: `tg-tui login` (or `--configure`)
- Detect: `tg-tui` invoked with no config + stdin attached to a TTY →
  either run the wizard inline OR print a one-liner
  "`run \`tg-tui login\` to set up`" and exit 1.
- `tg-tui login` flow:

```
$ tg-tui login
Welcome to tg-cli!  This one-time setup records your Telegram API
credentials into ~/.config/tg-cli/config.ini (mode 0600).

You need an api_id and api_hash from https://my.telegram.org.
  1. Log in with your phone number and the code Telegram sends.
  2. Go to 'API development tools'.
  3. Fill out the 'Create new application' form:
      - App title: anything, e.g. "my tg-cli"
      - Short name: any 5+ char slug
      - Platform: Desktop
      - Description: optional
  4. Copy the numeric 'App api_id' and the 32-char 'App api_hash'.
  5. Paste them below.

Press Ctrl-C to abort at any time.

Enter your api_id:   12345
Enter your api_hash: ********************************
Verifying...         OK (format valid)

Saved to ~/.config/tg-cli/config.ini (mode 0600).
Next: run 'tg-tui' and enter your phone number to complete login.
```

### 2. Validation
- api_id must parse as a positive 32-bit int.
- api_hash must be exactly 32 lowercase hex chars.
- Reject with a clear inline error on malformed input; re-prompt.
- Echo OFF for api_hash entry (mask).

### 3. File write
- `fs_mkdir_p(~/.config/tg-cli, 0700)` first.
- Write `config.ini` via atomic rename + fsync (same pattern as
  session_store).
- Chmod 0600.
- Refuse to overwrite an existing non-empty config without `--force`.

### 4. Also expose in the other binaries
- `tg-cli login` and `tg-cli-ro login` share the same wizard (via a
  common `src/app/config_wizard.{h,c}` module).

### 5. Batch-safe
- If stdin is NOT a TTY, fail with the current error message (no
  prompt hang).
- `--batch --api-id N --api-hash H` non-interactive write path for
  CI/scripted setup.

## Paired tests
- `tests/functional/test_config_wizard_happy.c` — pipe a valid
  api_id + api_hash on stdin, assert file written with mode 0600
  and correct content.
- `tests/functional/test_config_wizard_rejects_bad.c` — malformed
  inputs loop until valid; assert max 3 retries then exit 1.
- `tests/functional/pty/test_config_wizard_pty.c` — PTY test
  asserting the api_hash prompt does NOT echo to the master.

## Acceptance
- Fresh user can run `tg-tui login`, paste credentials, and the
  binary is ready to use.
- Existing config is never silently overwritten.
- All three binaries share the same wizard implementation.
- Man pages gain a `.SH CONFIGURATION` section pointing to the
  wizard.

## Dependencies
- None — pure new module.
- Coordinates with DOC-34 (my.telegram.org step-by-step guide).
