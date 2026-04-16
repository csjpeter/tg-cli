# Argument parser (custom)

## Description
Custom argument parser. Subcommands: dialogs, history, send, search, contacts, user-info, etc.
Global flags: --batch, --json, --quiet, --config, --help, --version.

## Estimate
~400 lines

## Dependencies
Nincs — önállóan végrehajtható (pure CLI logika, nincs protokoll függőség).

## Reviewed — 2026-04-16
Pass. Confirmed custom arg_parse (298 LOC) + header (81 LOC) with subcommands (dialogs/history/send/search/contacts/user-info/me/watch) and global flags (--batch, --json, --quiet, --config, --help, --version). No getopt dependency. Doxygen complete.
