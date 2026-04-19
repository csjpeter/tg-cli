# Documentation

## User Guide

- See the project [README](../README.md) for installation, configuration,
  interactive mode, and CLI reference.
- Man pages: [`man/tg-cli.1`](../man/tg-cli.1),
  [`man/tg-cli-ro.1`](../man/tg-cli-ro.1),
  [`man/tg-tui.1`](../man/tg-tui.1). After install they live under
  `/usr/local/share/man/man1/`.

## Product & Roadmap

- [SPECIFICATION.md](SPECIFICATION.md) — product specification (living).
- [userstory/README.md](userstory/README.md) — user story index
  (US-01..US-17).
- [userstory/US-00-roadmap.md](userstory/US-00-roadmap.md) — authoritative
  status / backlog snapshot.

## Developer Guides

- [Testing](dev/testing.md) — unit + functional + Valgrind layout, test
  inventory, how to add a new test.
- [Mock Telegram Server](dev/mock-server.md) — responder pattern,
  `mt_server_expect`, `mt_server_seed_session`, `with_tmp_home`,
  `bad_server_salt` injection.
- [Coverage](dev/coverage.md) — two-pass lcov, badges, GitHub Pages
  layout.
- [Logging](dev/logging.md) — log levels, rotation, MTProto traffic
  capture.
- [MTProto 2.0 Reference](dev/mtproto-reference.md) — protocol notes
  (handshake, encryption, transport).
- [Telegram Bot API reference](dev/telegram-bot-api.md) — Bot API
  surface kept for comparison (not used at runtime).
- [MTProto Phase 1 Plan](dev/mtproto-phase1-plan.md) — historical
  implementation plan, retained for provenance.
- [MTProto Phase 2 Plan](dev/mtproto-phase2-plan.md) — historical
  implementation plan, retained for provenance.

## Legal Research

- [Licensing, Open Source, and ToS Analysis](legal-research.md) — API ToS
  compliance, AI/ML clause, server public key, api_id requirements.

## Architecture Decision Records

- [ADR-0001: CLEAN Layered Architecture](adr/0001-clean-architecture.md)
- [ADR-0002: RAII Memory Safety](adr/0002-raii-memory-safety.md)
- [ADR-0003: Custom Test Framework](adr/0003-custom-test-framework.md)
- [ADR-0004: Dependency Inversion for Testability](adr/0004-dependency-inversion.md)
- [ADR-0005: Three-binary Architecture](adr/0005-three-binary-architecture.md)
- [ADR-0006: Three-level Test Strategy](adr/0006-test-strategy.md)
- [ADR-0007: In-process Mock Telegram Server](adr/0007-mock-telegram-server.md)
