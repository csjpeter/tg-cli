# Getting your api_id and api_hash

Every Telegram client — official or third-party — must identify itself with
an **api_id** (a short integer) and an **api_hash** (a 32-character hex
string).  You obtain these credentials once, for free, from Telegram's
developer portal at [my.telegram.org](https://my.telegram.org).

This guide walks a new user through the process end-to-end.

---

## Prerequisites

- A working Telegram account on any official client (Android, iOS, desktop).
- Access to the phone number registered with that account — you will receive
  a one-time code by SMS or Telegram message.
- A desktop web browser (Chrome, Firefox, Safari, Edge). Mobile browsers
  work but the form layout is cramped.

---

## Step 1 — Log in to my.telegram.org

(as of 2026 — verify at my.telegram.org)

1. Open **https://my.telegram.org** in your browser.
2. Enter your phone number in international format (e.g. `+15551234567`).
3. Click **Next**.  Telegram sends a one-time login code to your Telegram app
   (not SMS — the code arrives as a message from the "Telegram" system
   contact).
4. Enter the code in the **Confirmation code** field and click **Sign In**.

**2FA note:** my.telegram.org has its own 2FA gate — separate from your
Telegram account's cloud password.  If you have previously enabled two-step
verification on my.telegram.org, you will also be asked for that password.
Most accounts do not have it enabled; skip this step if no second prompt
appears.

**Expired code:** The code is valid for roughly five minutes.  If it expires,
click **Send code again** rather than reloading the whole page.

---

## Step 2 — Open "API development tools"

After login you land on the portal home page.  Click **API development tools**
in the navigation (as of 2026 — verify at my.telegram.org):

```
+------------------------------------------+
|  my.telegram.org                         |
|                                          |
|  [API development tools]  [My Apps] ...  |
+------------------------------------------+
```

If an application already exists for this phone number, you will see its
details immediately.  If no application exists, you will see the **Create
new application** form.

> **One app per account:** Telegram allows only one api_id per phone number.
> If you see an existing app, scroll down to copy the credentials rather than
> creating a second one.

---

## Step 3 — Fill out "Create new application"

Complete every mandatory field.  The table below lists each field, whether it
is required, and a safe value for a personal tg-cli installation.

| Field | Required? | Recommended value |
|---|---|---|
| **App title** | Yes | `my tg-cli` (or any descriptive label) |
| **Short name** | Yes | Any 5+ character slug, letters and digits only, e.g. `mytgcli` |
| **URL** | No | Leave blank |
| **Platform** | Yes | `Desktop` (closest match for a CLI tool) |
| **Description** | No | Leave blank, or `Personal CLI client` |

The form looks like this (as of 2026 — verify at my.telegram.org):

```
  Create new application
  ┌─────────────────────────────────────────┐
  │ App title:    [my tg-cli               ]│
  │ Short name:   [mytgcli                 ]│
  │ URL:          [                        ]│
  │ Platform:     [ Desktop            ▼  ]│
  │ Description:  [                        ]│
  │                                         │
  │             [  Create application  ]    │
  └─────────────────────────────────────────┘
```

Click **Create application**.

**FLOOD_WAIT error:** If the form returns a "FLOOD_WAIT" error (rate-limited),
wait the number of seconds indicated and try again.  This happens when the
form is submitted multiple times in quick succession — once is enough.

---

## Step 4 — Copy the generated credentials

After a successful submission the page shows your application's details.  You
need exactly two values:

| Field on page | Where it goes | Example |
|---|---|---|
| **App api_id** | `api_id` in config | `12345678` (6–8 digit integer) |
| **App api_hash** | `api_hash` in config | `a1b2c3d4e5f6...` (32 lowercase hex chars) |

```
  App api_id:    12345678
  App api_hash:  a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4

  Public Keys:   -----BEGIN RSA PUBLIC KEY-----
  Server URLs:   https://149.154.167.50
```

Copy the **Public Keys** and **Server URLs** values and compare them with
what tg-cli has built in.  They should match — Telegram uses the same
canonical RSA key and DC endpoints for every user.  If they differ from
what is shown above (e.g. Telegram has rotated the key), you can override
them in `~/.config/tg-cli/config.ini`:

```ini
; Optional overrides — only needed when Telegram changes the canonical values.
rsa_pem      = -----BEGIN RSA PUBLIC KEY-----\n...\n-----END RSA PUBLIC KEY-----
dc_1_host    = 149.154.175.50
dc_2_host    = 149.154.167.50
dc_3_host    = 149.154.175.100
dc_4_host    = 149.154.167.91
dc_5_host    = 91.108.56.130
```

> **Note:** config.ini override for `rsa_pem` and `dc_N_host` is tracked
> in [FEAT-38](../../issues/pending/FEAT-38-config-dc-rsa-override.md) and
> not yet implemented.  If you encounter a mismatch, please open an issue.

**Save your api_hash now.**  Telegram does not let you view it again after you
navigate away (as of 2026 — verify at my.telegram.org).  Copy it to a
password manager or secure note before closing the page.

---

## Step 5 — Feed the credentials into tg-cli

Choose whichever method fits your workflow.

### Option A — Interactive wizard (recommended for first-time setup)

```bash
tg-tui login
```

The wizard prompts for api_id (echoed) and api_hash (masked), validates both,
writes `~/.config/tg-cli/config.ini` with mode 0600, and exits.  All three
binaries (`tg-tui`, `tg-cli`, `tg-cli-ro`) share the wizard via
`tg-tui login` (or the equivalent `--configure` flag).

### Option B — Environment variables (useful in CI / containers)

```bash
export TG_CLI_API_ID=12345678
export TG_CLI_API_HASH=a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4
```

Add these lines to your shell profile (`~/.bashrc`, `~/.zshrc`, or equivalent)
to make them permanent.  Environment variables take precedence over the config
file.

### Option C — Manual config file

```bash
mkdir -p ~/.config/tg-cli
printf "api_id=12345678\napi_hash=a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4\n" \
    > ~/.config/tg-cli/config.ini
chmod 600 ~/.config/tg-cli/config.ini
```

The config file is a plain INI file; the only required keys are `api_id` and
`api_hash`.  The file **must** have mode 0600 — tg-cli refuses to read it if
it is world-readable.

---

## Step 6 — Verify

Run a quick self-info query to confirm that the credentials are accepted and
that the network path to Telegram's servers is open:

```bash
tg-cli-ro --phone +15551234567 --code 12345 me
```

(Replace `+15551234567` and `12345` with your own phone number and the SMS /
Telegram code you receive.)

### Success looks like

```
id:         987654321
first_name: Alice
last_name:  Smith
username:   alicesmith
phone:      +15551234567
```

### Failure looks like

| Error | Meaning |
|---|---|
| `ERROR: credentials: api_id/api_hash not found` | Step 5 is incomplete — the credentials were not saved |
| `RPC error: API_ID_INVALID` | The api_id is wrong (non-numeric, or mistyped) |
| `RPC error: API_HASH_INVALID` | The api_hash is wrong (length or character error) |
| `RPC error: PHONE_NUMBER_INVALID` | Phone number format wrong — use E.164 (`+15551234567`) |
| `RPC error: PHONE_CODE_INVALID` | The SMS/Telegram code was mistyped or expired |
| Connection refused / timeout | Network or firewall issue — Telegram uses port 443 (TCP) |

---

## Troubleshooting

### FLOOD_WAIT on the Create application form

Telegram rate-limits the form submission endpoint.  The error message includes
a wait time in seconds.  Wait, then submit once.  Refreshing the page and
re-submitting rapidly will increase the wait.

### Lost api_hash

Telegram does not display the api_hash after you navigate away from the app
detail page.  If you have lost it:

1. Visit my.telegram.org → API development tools → open your existing app.
2. Look for a "Revoke" or "Regenerate" button next to the api_hash field
   (as of 2026 — verify at my.telegram.org — the UI changes occasionally).
3. If no such button exists, contact [Telegram support](https://telegram.org/support).

**tg-cli does not auto-detect a revoked api_hash.**  If your hash is revoked,
any cached session will fail with `API_HASH_INVALID` on the next MTProto
handshake; delete `~/.config/tg-cli/config.ini` and re-run setup with the new
hash.

### Account flagged for sharing api_id

Telegram tracks usage per api_id.  If many different accounts use the same
api_id (e.g., a hash found in a public GitHub gist), Telegram will throttle
or ban that id.  Always generate your own — it is free and takes under five
minutes.

### 2FA on my.telegram.org (portal password, not Telegram cloud password)

my.telegram.org supports an independent two-step verification for the portal
itself.  If you enabled this and forgot the password, use the
**Forgot password?** link on the portal sign-in page to receive a reset code
via your Telegram account.

### Browser blocked / Captcha loop

Some privacy-focused browser extensions (ad blockers, script blockers) can
interfere with the my.telegram.org form.  Try disabling extensions or using a
different browser / private window if the form does not submit.

---

## Security

### Never commit api_hash to version control

Your api_hash identifies your Telegram application to the MTProto servers.
Committing it to a public repository exposes it to automated scrapers that
will throttle or block it within hours.

**Before every `git commit`**, verify the hash is not in any tracked file:

```bash
git diff --cached | grep -i api_hash
```

Consider adding this to `.gitignore`:

```
.env
config.ini
*_credentials*
```

### Do not reuse hashes found online

Public GitHub gists and tutorial repositories often include real api_hashes.
These are shared across many users and are routinely throttled by Telegram.
Your own credentials are free — always generate them yourself.

### Rotation procedure

If you believe your api_hash has been compromised:

1. Log in to **my.telegram.org → API development tools**.
2. Revoke (or delete and recreate) the application (as of 2026 — verify at
   my.telegram.org).
3. Copy the new api_hash.
4. Run `tg-tui login --force` (or edit `~/.config/tg-cli/config.ini`) to
   replace the stored hash.
5. Delete the old session: `rm ~/.config/tg-cli/session.bin`.
6. Log in again with `tg-tui login`.

### File permissions

`~/.config/tg-cli/config.ini` and `~/.config/tg-cli/session.bin` must have
mode 0600 (owner read/write only).  tg-cli enforces this on write; if you
edit the file manually with a tool that resets permissions, run
`chmod 600 ~/.config/tg-cli/config.ini ~/.config/tg-cli/session.bin`
afterwards.
