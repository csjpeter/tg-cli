# Developer Guide: Logging

## Overview

`tg-cli`, `tg-cli-ro`, and `tg-tui` share a single rotating file logger that
writes diagnostic output to `~/.cache/tg-cli/logs/session.log`. No log data
is sent over the network. MTProto traffic is captured by the RPC layer at
DEBUG level when explicitly enabled (see below) — there is **no HTTP stack
and no libcurl**, so there is no `CURLINFO_*` dump to inspect.

## Log Levels

| Level | Meaning |
|-------|---------|
| `LOG_DEBUG` | Infrastructure diagnostics: cache operations, service frame filtering (acks, pongs). Per-frame MTProto envelope dumps (auth_key_id, msg_key, msg_id, seq_no, TL CRC) are **planned** (FEAT ticket pending). |
| `LOG_INFO` | Application milestones: startup, config loaded, DC connect, handshake complete, session persisted, clean shutdown. |
| `LOG_WARN` | Non-fatal issues: `bad_server_salt` retry, `FILE_MIGRATE_X`, missing optional config field, permission warning on `session.bin`. |
| `LOG_ERROR` | Fatal errors: connection refused, DH verification failure, TL parse error. Also echoed to stderr. |

The default runtime level is `LOG_INFO`. Raise to `LOG_DEBUG` in `main.c`
(`logger_init(path, LOG_DEBUG)`) when diagnosing a specific session.

## Log Files

```
~/.cache/tg-cli/logs/
  session.log        ← current session
  session.log.1      ← previous session
  ...
  session.log.5      ← oldest kept
```

Rotation triggers when `session.log` exceeds **5 MB**. The oldest file
(`session.log.5`) is deleted; each existing file shifts up by one.

## MTProto Traffic Capture (Planned)

**Status:** Per-frame MTProto envelope logging is not yet implemented.

Planned future feature: at `LOG_DEBUG`, the RPC layer (`src/infrastructure/mtproto_rpc.c` +
`src/infrastructure/api_call.c`) will emit, per frame:

- direction (`→ server` / `← server`),
- outer 4-byte abridged-transport length prefix,
- `auth_key_id` (8 bytes), `msg_key` (16 bytes), ciphertext length,
- decrypted inner envelope: `salt`, `session_id`, `msg_id`, `seq_no`,
  `length`, TL constructor CRC32.

The TL body itself will **not be dumped** — it can carry message bodies, 2FA tokens,
or access_hashes. A further planned feature: `TG_CLI_LOG_PLAINTEXT=1` env var for opt-in plaintext logging on test accounts.

## Purging Logs

```bash
./manage.sh clean-logs
```

Removes every `session.log*` file under `~/.cache/tg-cli/logs/`.
The binaries themselves do not take a `--clean-logs` flag.

## Using the Logger in Code

```c
#include "logger.h"

logger_log(LOG_INFO,  "Connected to DC%d (%s:%d)", dc_id, host, port);
logger_log(LOG_WARN,  "bad_server_salt; retrying with new salt 0x%016llx", salt);
logger_log(LOG_ERROR, "Failed to open %s: %s", path, strerror(errno));
```

The logger is a global singleton. Call `logger_init()` once in `main.c`
before any other module uses it. Call `logger_close()` at exit. Both are
idempotent — re-init inside a test harness is safe.

## Tests

`tests/unit/test_logger.c` covers the level filtering, rotation trigger,
and idempotency guarantees. Each test uses `with_tmp_home()` (see
`tests/common/test_helpers.h`) to redirect `$HOME` so log writes land in a
per-test temp directory.
