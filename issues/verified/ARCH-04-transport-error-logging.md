# Add error logging to transport.c

## Description
`src/infrastructure/transport.c` has ~10 error return paths that return -1
silently without any logging. Every other infrastructure file (mtproto_auth.c,
config_store.c, api_call.c) logs errors before returning, but transport.c
is the exception.

This makes debugging connection and protocol framing issues very difficult
since the caller only sees -1 with no diagnostic context.

## Steps
1. Add `#include "logger.h"` to transport.c
2. Add `logger_log(LOG_ERROR, "transport: ...")` before each `return -1` with
   a message describing the specific failure (connect failed, send failed,
   recv failed, invalid frame length, etc.)
3. Verify: `./manage.sh test && ./manage.sh valgrind`

## Estimate
~15 lines

## Dependencies
Nincs — önállóan végrehajtható.

## Reviewed — 2026-04-16
Pass. Confirmed `#include "logger.h"` added to transport.c and logger_log(LOG_ERROR,...) on every -1 return (socket create, connect, marker send, length prefix send/recv, payload send/recv, frame-too-large). Every error path now carries diagnostic context.
