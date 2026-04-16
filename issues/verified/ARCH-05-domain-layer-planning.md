# Plan domain layer structure for P5+ features

## Description
`src/domain/` exists but is empty. As P5 (messaging), P6 (media), and P7
(contacts) approach, Telegram-specific business logic will need a home.
Without a clear domain layer plan, there is risk that business logic leaks
into infrastructure (mtproto_rpc, api_call) or gets crammed into main.c.

The domain layer should sit between main.c and infrastructure, orchestrating
API calls and transforming raw TL responses into application-level data
structures.

## Steps
1. Define domain module boundaries:
   - `src/domain/dialog.{h,c}` — dialog list, unread counts, pinned
   - `src/domain/message.{h,c}` — message history, search, send
   - `src/domain/media.{h,c}` — file download/upload, media info
   - `src/domain/contact.{h,c}` — contacts, user info, username resolution
2. Define domain data structures (Dialog, Message, User, etc.) that are
   independent of TL wire format
3. Document the domain→infrastructure boundary: domain calls api_call.h
   functions, receives raw TL, and parses into domain structs
4. Add domain/ to CMake include paths and tg-proto library
5. Create initial header files with Doxygen comments (implementation in
   respective P5-P9 issues)

## Estimate
~200 lines (headers + structs only, implementation is P5-P9 scope)

## Dependencies
- ARCH-01 (pending) — domain/ hozzáadása a tg-proto static library-hoz

## Verified — 2026-04-16 (v1)
- `src/domain/read/` now holds self, dialogs, history, updates,
  user_info, search modules. Writes live under `src/domain/write/`
  (empty; ADR-0005 defines the split).
- `src/app/` carries bootstrap, auth_flow, credentials, dc_config,
  session_store — shared by all binaries.
- ADR-0005 documents the split.
