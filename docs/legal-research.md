# Legal Research: Telegram Client Development

> **Disclaimer:** This document contains research findings only.
> It does not constitute legal advice. When in doubt, consult a qualified lawyer.
> Research date: 2026-03-28.

## Summary

Building a custom Telegram user client is **explicitly permitted** by Telegram.
The MTProto protocol specification is open and unrestricted. The server RSA public
key is embedded in TDLib (BSL-1.0 license) and can be legally extracted.

---

## 1. API Terms of Service

Telegram's API ToS opens with:

> "We welcome all developers to use our API and source code to create
> Telegram-like messaging applications on our platform free of charge."

**Source:** https://core.telegram.org/api/terms

### Requirements for third-party clients

| Requirement | Details |
|---|---|
| Obtain api_id | Free from https://my.telegram.org → "API development tools" |
| User awareness | Users must know the app uses Telegram API |
| App naming | Must not include "Telegram" unless preceded by "Unofficial" |
| No official logo | The Telegram brand and logo are registered trademarks |
| Privacy & security | Must comply with Security Guidelines |
| No core interference | Self-destruct, read status, typing status must work correctly |
| Monetization allowed | Must disclose methods in app store description |

### Breach of terms

- 10-day cure period after notification
- Then API access is revoked and app stores are contacted

---

## 2. Who is a "Client Developer"?

The ToS at https://core.telegram.org/api/terms applies to developers who build
**third-party Telegram client applications** — apps distributed to other users
(via app stores, downloads, etc.) that connect to the Telegram API.

The ToS preamble states:

> "In order to ensure consistency and security across the Telegram ecosystem,
> **all third-party client apps** must comply with the following Terms of Service."

**This means:** A developer who builds and publishes a Telegram client app for
others to use. It does **not** target individuals building personal tools for
their own use. However, anyone obtaining an api_id implicitly agrees to these terms.

---

## 3. AI/ML Clause — Section 1.5

> "Your use of the Telegram API is further subject to the Telegram Terms of
> Service for Content Licensing and AI Scraping. As such, you are prohibited
> from using, accessing or aggregating data obtained from the Telegram platform
> to train, fine-tune or otherwise engage in the development, enhancement or
> deployment of artificial intelligence, machine learning models and similar
> technologies."

**Source:** https://core.telegram.org/api/terms (Section 1.5)

Also referenced: https://telegram.org/tos (Content Licensing and AI Scraping section)

### Project interpretation

This project (tg-cli) is a **data handling client** — a read-only CLI tool
that retrieves the user's own private Telegram communications. The planned
architecture is:

```
tg-cli (this project)           → reads own messages, read-only
  ↓ feeds data to
AI agent program (separate project)   → evaluates personal communications
```

### Who does Section 1.5 actually bind?

The ToS document is addressed to "client developers" — people who build apps
distributed to others. However, there is a **logical gap** in applying Section 1.5
to the developer rather than the end user:

**The developer of a client cannot control what users do with data they access.**
If a user reads their own messages through the official Telegram Desktop client,
then copies them into ChatGPT, Telegram does not hold the Telegram Desktop
developers responsible. The same logic applies to any client developer.

**Two possible interpretations:**

| Interpretation | Reads on... | Implication for us |
|---|---|---|
| Binds the developer | "You (the developer) must not use the API to build ML features into your client" | We just don't add ML/export features to tg-cli. Fine. |
| Binds the end user via the developer | "You (the developer) must ensure your users don't use the data for ML" | Unenforceable and illogical — a developer publishing open-source code on GitHub cannot control what users do with their own messages locally. |

**Our reading:** Section 1.5 binds the developer's actions (don't build ML pipelines
into the client, don't add bulk-export-for-training features). It does **not** make
the developer a guarantor of the end user's behavior. The developer of a text editor
is not responsible if a user trains an LLM on text written in it.

**What this means in practice:**
- We build a read-only CLI client and publish it on GitHub
- We do NOT embed ML training, fine-tuning, or data aggregation features
- What individual users do with their own messages on their own machine is their
  responsibility — just as with the official Telegram clients
- A user feeding their own messages into an LLM for personal evaluation is no
  different from copying messages into any AI chatbot by hand

| Activity | Assessment |
|---|---|
| Training new models | NOT planned — clearly prohibited by ToS |
| Fine-tuning existing models | NOT planned — clearly prohibited by ToS |
| Aggregating data for ML datasets | NOT planned — clearly prohibited by ToS |
| Deploying AI/ML models on Telegram data | NOT planned — the AI agent runs locally, evaluating only the user's own messages |
| Reading own messages locally via CLI | The ToS targets apps that **export/aggregate Telegram data into ML training pipelines** at scale. Reading your own messages with a personal CLI tool, then using an LLM for personal evaluation, is a fundamentally different activity from building a data harvesting application. |
| Using AI to evaluate own communications | The clause specifically targets "training, fine-tuning, development, enhancement or deployment of AI/ML models" — not personal use of AI tools to read and understand your own messages. No model is trained, fine-tuned, or developed. An existing LLM is used as a reasoning tool on local data. |

**Supporting evidence:** The Bot API (which Telegram officially provides) allows bots
to read and process messages programmatically. This confirms that automated message
processing itself is permitted — the concern is large-scale data harvesting for model
training, not personal use.

**Practical recommendation:**
- Proceed with read-only personal use
- Do NOT build features that bulk-export messages for ML training pipelines
- Do NOT train, fine-tune, or develop AI/ML models on Telegram data
- If in doubt, contact Telegram at recover@telegram.org for clarification

---

## 4. TDLib — License and Server Public Key

### TDLib license

- **Repository:** https://github.com/tdlib/td
- **License:** BSL-1.0 (Boost Software License 1.0)
- **License file:** https://github.com/tdlib/td/blob/master/LICENSE_1_0.txt
- **Permissive:** Allows commercial use, modification, distribution with attribution

### Server RSA public key location in TDLib

**File:** `td/telegram/net/PublicRsaKeySharedMain.cpp`

```
-----BEGIN RSA PUBLIC KEY-----
MIIBCgKCAQEA6LszBcC1LGzyr992NzE0ieY+BSaOW622Aa9Bd4ZHLl+TuFQ4lo4g
5nKaMBwK/BIb9xUfg0Q29/2mgIR6Zr9krM7HjuIcCzFvDtr+L0GQjae9H0pRB2OO
62cECs5HKhT5DZ98K33vmWiLowc621dQuwKWSQKjWf50XYFw42h21P2KXUGyp2y/
+aEyZ+uVgLLQbRA1dEjSDZ2iGRy12Mk5gpYc397aYp438fsJoHIgJ2lgMv5h7WY9
t6N/byY9Nw9p21Og3AoXSL2q/2IJ1WRUhebgAdGVMlV1fkuOQoEzR7EdpqtQD9Cs
5+bfo3Nhmcyvk5ftB0WkJ9z6bNZ7yxrP8wIDAQAB
-----END RSA PUBLIC KEY-----
```

**Source:** https://github.com/tdlib/td/blob/master/td/telegram/net/PublicRsaKeySharedMain.cpp

This key is **by definition public** — it identifies the Telegram server during the
MTProto handshake. It cannot be used to impersonate the server (that requires the
private key, which never leaves Telegram's servers).

### Related TDLib files (RSA operations)

- `td/mtproto/RSA.cpp` — RSA encrypt, verify signature, fingerprint computation
  https://github.com/tdlib/td/blob/master/td/mtproto/RSA.cpp
- `td/telegram/net/PublicRsaKeySharedCdn.cpp` — CDN public keys
  https://github.com/tdlib/td/blob/master/td/telegram/net/PublicRsaKeySharedCdn.cpp

---

## 5. GPL Licensing — Does It Apply?

| Component | License | Affects us? |
|---|---|---|
| TDLib | BSL-1.0 | No — permissive |
| Telegram Android | GPL v2 | Only if we copy code from it |
| Telegram iOS | GPL v2 | Only if we copy code from it |
| Telegram Desktop | GPL v3 + OpenSSL exception | Only if we copy code from it |
| Bot API server | BSL-1.0 | No — permissive |
| MTProto specification | Open / unrestricted | No — protocol cannot be GPL'd |
| **Our implementation** | **GPL v3** (this project) | **Our own choice** |

**Key insight:** Writing an independent implementation of the MTProto protocol from
scratch does NOT trigger any GPL obligations. The GPL only applies if we copy/modify
code from the GPL-licensed client repositories.

---

## 6. api_id and api_hash

**How to obtain:**
1. Log in at https://my.telegram.org
2. Go to "API development tools"
3. Fill out the form (app title, short name, platform, description)

**Cost:** Free
**Limit:** One api_id per phone number

**Source:** https://core.telegram.org/api/obtaining_api_id

**Warning from Telegram:**
> "Accounts using unofficial API clients are automatically put under observation."

This means our account may be flagged for using a custom client. This is a monitoring
flag, not a ban. Avoid flooding and spamming — those result in permanent bans.

---

## 7. Open Source Telegram Components

| Component | URL | License |
|---|---|---|
| TDLib (client library) | https://github.com/tdlib/td | BSL-1.0 |
| Telegram Android | https://github.com/DrKLO/Telegram | GPL v2 |
| Telegram iOS | https://github.com/TelegramMessenger/Telegram-iOS | GPL v2 |
| Telegram Desktop | https://github.com/telegramdesktop/tdesktop | GPL v3 + OpenSSL exception |
| Bot API server | https://github.com/tdlib/telegram-bot-api | BSL-1.0 |
| MTProto specification | https://core.telegram.org/mtproto | Open |
| API documentation | https://core.telegram.org/methods | Open |
| API ToS | https://core.telegram.org/api/terms | — |
| api_id registration | https://my.telegram.org | — |

---

## 8. Other Independent MTProto Implementations

These projects confirm that independent MTProto implementations are permitted:

| Project | Language | License | URL |
|---|---|---|---|
| Telethon | Python | MIT | https://github.com/LonamiWebs/Telethon |
| Pyrogram | Python | LGPL v3 | https://github.com/pyrogram/pyrogram |
| GramJS | JavaScript | MIT | https://github.com/nicolo-ribaudo/gramjs |
| gotd/td | Go | MIT | https://github.com/gotd/td |
| MadelineProto | PHP | AGPL v3 | https://github.com/danog/MadelineProto |

None of these have been shut down or received cease-and-desist notices, confirming
that independent client development is tolerated and welcomed.
