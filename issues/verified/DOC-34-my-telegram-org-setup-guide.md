# DOC-34 — Detailed my.telegram.org setup guide (get api_id / api_hash)

## Gap
`README.md:180-189` currently has only 4 short lines on this:

```
You need an api_id and api_hash from Telegram (free):

1. Log in at https://my.telegram.org
2. Go to "API development tools"
3. Fill out the form
```

A user landing at my.telegram.org for the first time has no guidance on:

1. How to log in (you need a Telegram account + SMS code on your phone).
2. What exactly to fill in the "Create new application" form
   (App title, Short name, URL, Platform, Description — which are
   mandatory, which can be left blank, what is safe to type).
3. Which of the generated values go where:
   - "App api_id" → `api_id` in config.
   - "App api_hash" → `api_hash` in config.
   - Public keys / MTProto server URLs → NOT used by tg-cli (tg-cli
     uses the bundled DC endpoints).
4. Rate limits and common issues:
   - "FLOOD_WAIT" error when filling the form too fast.
   - "Only one API ID per phone number" — if you lost the hash,
     you cannot regenerate without contacting Telegram.
   - What happens if multiple users share the same api_id (the
     account gets flagged).
5. Security considerations:
   - Never commit api_hash to public repos.
   - Don't use an api_hash found on GitHub gists (they are throttled).
6. How to verify the binary actually accepted the credentials
   (run `tg-cli-ro me` and see the server response).

## Scope
1. Create `docs/user/setup-my-telegram-org.md`:
   - Step-by-step with screenshots (or ASCII mockups of the
     my.telegram.org form).
   - Cover every field + recommended value.
   - Troubleshooting section (FLOOD_WAIT, expired SMS code, 2FA on
     my.telegram.org).
   - Security section: what NOT to do with api_hash.
2. Replace the 4-line block in `README.md` with a one-line pointer
   to `docs/user/setup-my-telegram-org.md`.
3. Add a `.SH CONFIGURATION` section to all three man pages
   pointing to this doc.
4. Cross-reference FEAT-37's wizard: the wizard's welcome text
   should link to the same URL + doc path.

## Proposed doc outline
```
# Getting your api_id and api_hash

## Prerequisites
  - A working Telegram account on any client.
  - Access to the phone number registered with that account.

## Step 1 — Log in to my.telegram.org
  Browser flow, SMS code delivery, 2FA notes.

## Step 2 — Open "API development tools"
  Where the link is, what the page looks like.

## Step 3 — Fill out "Create new application"
  Field-by-field table:
    App title:     anything that identifies the client to you
    Short name:    5+ chars, letters and digits, unique per account
    URL:           optional — leave blank
    Platform:      "Desktop" (closest match for a CLI)
    Description:   optional — anything reasonable
  Save / submit.

## Step 4 — Copy the generated credentials
  "App api_id"   → a 6–8 digit number
  "App api_hash" → a 32-char lowercase hex string
  "Public Keys" and "Server URLs" should match tg-cli's built-in values.
  If Telegram has changed them, override via rsa_pem / dc_N_host in config.ini
  (see FEAT-38).

## Step 5 — Put the credentials into tg-cli
  Option A — interactive:  tg-tui login
  Option B — env:          export TG_CLI_API_ID=...
                           export TG_CLI_API_HASH=...
  Option C — config file:  printf "api_id=...\napi_hash=...\n" > \
                             ~/.config/tg-cli/config.ini
                           chmod 600 ~/.config/tg-cli/config.ini

## Step 6 — Verify
  tg-tui login && tg-tui
  (or tg-cli-ro --phone +... --code ... me)

## Troubleshooting
  FLOOD_WAIT on the form, lost api_hash, account flagged, …

## Security
  Keep api_hash private.  Do not commit it.  Rotate by deleting
  the app in my.telegram.org and re-creating.
```

## Acceptance
- New user succeeds at login in under 10 minutes with only this doc.
- `README.md` Prerequisites section is 2 lines (pointer only).
- Man page CONFIGURATION section points here.
- Wizard (FEAT-37) embeds this URL in its welcome text.

## Dependencies
- Coordinates with FEAT-37 but can land independently.
- Pure documentation — no code changes required.
