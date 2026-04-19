# Security Policy

## Scope

tg-cli handles sensitive data including Telegram authentication keys, MTProto 2.0 cryptographic material, and private message content. Security reports are welcome for:

- **In scope:** Authentication key derivation (ECDH DH), session storage (`~/.cache/tg-cli/session.bin`), MTProto encryption/decryption, credential management.
- **Out of scope:** Telegram's API itself, upstream OpenSSL vulnerabilities (report to OpenSSL directly), or platform-specific terminal security (report to your OS vendor).

## Reporting a Vulnerability

Please report security vulnerabilities by emailing **peter@csaszar.email** with:

- A clear description of the issue
- Steps to reproduce (if applicable)
- Affected version(s)

Alternatively, use [GitHub's private vulnerability reporting](https://github.com/petercz/tg-cli/security/advisories/new) if you have a GitHub account.

## Response Timeline

- **Acknowledgement:** Within 3 business days
- **Fix and disclosure:** Within 14 days for critical issues; 30 days for non-critical
- **Supported versions:** main branch and stable releases (currently 0.1.0+)

## Responsible Disclosure

We ask that you not publicly disclose the vulnerability until we've had time to prepare and release a fix. Thank you for helping keep tg-cli secure.
