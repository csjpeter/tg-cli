# FEAT-24 — int32_t date fields will overflow in 2038 (Telegram API + Android NDK risk)

## Category
Feature / Portability

## Severity
Low (long lead time but zero-cost to fix now)

## Finding
Several domain structs store message timestamps as `int32_t`:

- `/home/csjpeter/ai-projects/tg-cli/src/domain/read/history.h:31` — `int32_t date;`
- `/home/csjpeter/ai-projects/tg-cli/src/domain/read/updates.h:23` — `int32_t date;`

The Telegram TL schema currently sends `date` as a 32-bit Unix timestamp.
`int32_t` overflows on 2038-01-19.  While the Telegram API has not announced
a migration, the C-side representation can be widened to `int64_t` at no
protocol cost (the extra bits stay zero until Telegram extends the wire format).

Additionally, Android NDK pre-r21 on 32-bit ARMv7 still uses a 32-bit `time_t`,
which makes any code that converts `date` → `time_t` → `localtime()` vulnerable
on those targets.

## Evidence
- `/home/csjpeter/ai-projects/tg-cli/src/domain/read/history.h:31`
- `/home/csjpeter/ai-projects/tg-cli/src/domain/read/updates.h:23`
- `src/core/logger.c:95` — `time_t now = time(NULL)` (platform `time_t` width)

## Fix
1. Change `int32_t date` to `int64_t date` in `history.h`, `updates.h`,
   and all consuming structs/functions.
2. In the TL reader, sign-extend the 32-bit wire value when populating the field.
3. Format output via `(long long)entry.date` to avoid `%d` truncation on LP64.
4. Document in `CLAUDE.md` portability table: "Android NDK 32-bit: time_t is 32-bit
   — use NDK r21+ where `time_t` is 64-bit on all ABI targets".
