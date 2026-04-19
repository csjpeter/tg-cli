#!/bin/bash
# ci/check-ro-isolation.sh
#
# Verifies that bin/tg-cli-ro contains none of the write-domain symbols.
# ADR-0005 mandates compile-time read-only isolation; this script is the
# runtime confirmation that the CMake link-set is still correct.
#
# Usage:  ./ci/check-ro-isolation.sh [path/to/tg-cli-ro]
# Exit:   0 = no write symbols found (isolation is intact)
#         1 = write symbol(s) found, or binary not found

set -euo pipefail

BINARY="${1:-bin/tg-cli-ro}"

if [ ! -f "$BINARY" ]; then
    echo "ERROR: binary not found: $BINARY" >&2
    echo "Run './manage.sh build' first." >&2
    exit 1
fi

# Symbols exported by tg-domain-write that must never appear in tg-cli-ro.
WRITE_SYMBOLS=(
    domain_send_message
    domain_send_message_reply
    domain_send_file
    domain_send_photo
    domain_edit_message
    domain_delete_messages
    domain_forward_messages
    domain_mark_read
    upload_chunk_phase
)

# Build an ERE pattern accepted by grep.
PATTERN=$(printf '%s|' "${WRITE_SYMBOLS[@]}")
PATTERN="${PATTERN%|}"   # strip trailing pipe

echo "Checking $BINARY for write-domain symbols..."

# nm --defined-only lists only symbols defined (not merely referenced) in the
# binary — undefined/imported symbols are excluded.  We use the full nm output
# (not --defined-only) so that any accidentally-linked definition is caught.
FOUND=$(nm "$BINARY" 2>/dev/null | grep -E "\\b($PATTERN)\\b" || true)

if [ -n "$FOUND" ]; then
    echo "FAIL: write-domain symbol(s) found in $BINARY:" >&2
    echo "$FOUND" >&2
    echo "" >&2
    echo "tg-cli-ro must never link tg-domain-write (ADR-0005)." >&2
    exit 1
fi

echo "OK: no write-domain symbols found in $BINARY."
exit 0
