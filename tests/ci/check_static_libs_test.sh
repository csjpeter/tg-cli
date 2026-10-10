#!/bin/sh
# SPDX-License-Identifier: LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Proprietary; the owner is the author.
# Created: 2026-10-11
# Tests ci/check-static-libs.sh against a fake ldd, accepting and rejecting.

set -eu
src="$1"
work="$2"
script="$src/ci/check-static-libs.sh"
mkdir -p "$work"
: > "$work/binary"

cat > "$work/ldd-clean" <<'FAKE'
#!/bin/sh
printf '\tlibssl.so.3 => /lib/libssl.so.3 (0x1)\n\tlibc.so.6 => /lib/libc.so.6 (0x2)\n'
FAKE
cat > "$work/ldd-tuikit" <<'FAKE'
#!/bin/sh
printf '\tlibtuikit.so.0 => /usr/lib/libtuikit.so.0 (0x1)\n\tlibc.so.6 => /lib/libc.so.6 (0x2)\n'
FAKE
cat > "$work/ldd-ptytest" <<'FAKE'
#!/bin/sh
printf '\tlibptytest.so.0 => /usr/lib/libptytest.so.0 (0x1)\n'
FAKE
chmod +x "$work"/ldd-*

fail=0
LDD="$work/ldd-clean" "$script" "$work/binary" || { echo "FAIL: clean binary rejected" >&2; fail=1; }
if LDD="$work/ldd-tuikit" "$script" "$work/binary"; then echo "FAIL: libtuikit.so accepted" >&2; fail=1; fi
if LDD="$work/ldd-ptytest" "$script" "$work/binary"; then echo "FAIL: libptytest.so accepted" >&2; fail=1; fi
if LDD="$work/ldd-clean" "$script" "$work/missing"; then echo "FAIL: missing binary accepted" >&2; fail=1; fi
exit "$fail"
