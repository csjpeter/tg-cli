#!/bin/sh
# SPDX-License-Identifier: LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Proprietary; the owner is the author.
# Created: 2026-10-11
# Tests ci/timing.sh: the record line, the Gate line, and "not measured".

set -eu
src="$1"
work="$2"
script="$src/ci/timing.sh"
mkdir -p "$work"
log="$work/timing.log"
rm -f "$log"
fail=0
check() { # description expected actual
    if [ "$2" != "$3" ]; then
        echo "FAIL: $1: expected '$2', got '$3'" >&2
        fail=1
    fi
}

# Missing log: honest gap and a non-zero exit.
out=$(sh "$script" gate "$log") && { echo "FAIL: missing log exit 0" >&2; fail=1; }
check "missing log" "Gate: not measured" "$out"
: > "$log"
out=$(sh "$script" gate "$log") && { echo "FAIL: empty log exit 0" >&2; fail=1; }
check "empty log" "Gate: not measured" "$out"

# Only verbs that are not a gate part: still not measured.
sh "$script" record "$log" build 100 107 0
out=$(sh "$script" gate "$log") && { echo "FAIL: build-only exit 0" >&2; fail=1; }
check "build only" "Gate: not measured" "$out"

# Record line shape, success and failure.
sh "$script" record "$log" check 1000 1042 1
line=$(tail -n 1 "$log")
printf '%s\n' "$line" | grep -Eq '^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:]{8}Z check 42s fail$' \
    || { echo "FAIL: record shape: $line" >&2; fail=1; }
out=$(sh "$script" gate "$log")
check "check only" "Gate: 42s (check 42s)" "$out"

# Latest run of each verb wins; the missing part is left out.
sh "$script" record "$log" check 2000 2030 0
sh "$script" record "$log" package 2030 2050 0
out=$(sh "$script" gate "$log")
check "check+package" "Gate: 50s (check 30s, package 20s)" "$out"
sh "$script" record "$log" install-deb 2050 2055 0
out=$(sh "$script" gate "$log")
check "all three" "Gate: 55s (check 30s, package 20s, install 5s)" "$out"
exit "$fail"
