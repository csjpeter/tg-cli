# SEC-02: Partial phone_code_hash leaked to LOG_INFO log file

## Severity
Low (partial secret in unprotected log file)

## Location
`src/infrastructure/auth_session.c`, line ~153:
```c
logger_log(LOG_INFO, "auth_send_code: code sent, hash=%.12s... timeout=%d", ...)
```

## Problem
`phone_code_hash` is a session-scoped secret token used in `auth.signIn`.
Logging even the first 12 characters exposes roughly 60 bits of entropy to
anyone with read access to `~/.cache/tg-cli/logs/session.log` (which is **not**
mode 0600 — only `session.bin` is restricted).

`logging.md` explicitly states: *"The TL body itself is not dumped by default —
it can carry message bodies, 2FA tokens, or access_hashes."*  A partial hash
leaking at `LOG_INFO` contradicts that policy.

## Fix direction
Replace the hash fragment with a fixed placeholder or its length only:
```c
logger_log(LOG_INFO, "auth_send_code: code sent (hash_len=%zu), timeout=%d",
           strlen(sent->phone_code_hash), (int)sent->timeout);
```
Set `session.log` permissions to 0600 on creation (see related gap in
DOC-13 / logger init).

## Test
Unit test: after calling `auth_send_code` via mock, read back the log file and
assert the phone_code_hash string does **not** appear anywhere in it.
