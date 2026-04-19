# DOC-32 — tg-cli --help missing concrete examples

## Gap
`tg-cli --help` (see `print_usage` in `tg_cli.c:326-367`) does not
include any worked examples.  A new user sees the flag list and
must consult man pages or `tg-cli-ro --help` to figure out syntax.

`tg-cli-ro --help` is equally bare on examples.

## Scope
1. Add an `Examples:` block to `print_usage` in `tg_cli.c`:
   ```
   Examples:
     tg-cli send @alice "hello"
     echo "hello" | tg-cli send @alice
     tg-cli edit @alice 42 "updated text"
     tg-cli delete @alice 42 --revoke
     tg-cli forward @alice @bob 42
     tg-cli send-file @alice /tmp/doc.pdf --caption "Report"
     tg-cli read @alice --max-id 100
   ```
2. Do the same for `tg-cli-ro` with one example per subcommand.
3. Match with the `.SH EXAMPLES` section in man pages (they already
   have some — unify).

## Acceptance
- `tg-cli --help` ends with an Examples block.
- `tg-cli-ro --help` similarly.
- Each example is copy-paste runnable against the mock server in a
  paired smoke test.

## Dependencies
- None.
