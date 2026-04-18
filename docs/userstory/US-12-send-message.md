# US-12 — Send message + read markers

Applies to: `tg-tui`, `tg-cli`. **Never** `tg-cli-ro`.

**Status:** done — `messages.sendMessage` (plain + `--reply` +
stdin-pipe), `messages.readHistory` / `channels.readHistory` dispatched
by peer kind (P5-03, P5-04, P8-03).

## Story
As a user I want to send a plain-text message from the TUI or from a batch
command so that I can reply without switching to another client.

## Scope
`messages.sendMessage` (text only in v1). Read markers (`messages.readHistory`)
come with this story so that a "read and reply" flow works.

## UX
- TUI: `:send <text>` command, or focus the input line in the open chat.
- `tg-cli send <peer> <message>` batch form.

## Acceptance
- Not linkable by `tg-cli-ro` (ADR-0005 — write code lives under
  `src/domain/write/`).
- Handles `FLOOD_WAIT_X` by surfacing a clear error and exit code.
- Confirms delivery (`updates.sentMessage` or similar) before returning 0.

## Dependencies
Full tg-cli-ro roadmap must be stable first.
