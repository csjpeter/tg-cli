# DOC-11 — tg-cli-ro man page missing exit code 2 (write command attempted)

## Gap
`src/main/tg_cli_ro.c:832` returns exit code 2 when a write subcommand
(e.g. `send`) is attempted in the read-only binary:
```c
case CMD_SEND:
    fprintf(stderr, "tg-cli-ro: send is not available in read-only mode\n");
    exit_code = 2; break;
```
`man/tg-cli-ro.1` EXIT STATUS section only documents exit codes 0 and 1.
Exit code 2 is silently omitted, so scripts that distinguish write-in-ro
errors from authentication/RPC errors cannot rely on documented behaviour.

## Scope
Add an exit code 2 entry to the `.SH EXIT STATUS` section of
`man/tg-cli-ro.1`:
```
.TP
.B 2
A write subcommand was invoked on the read-only binary. See
.BR tg-cli (1)
for write operations.
```

## Acceptance
- `man/tg-cli-ro.1` EXIT STATUS section documents exit code 2.
- `groff -man` produces no warnings.
