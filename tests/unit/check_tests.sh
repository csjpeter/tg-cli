#!/bin/sh
# SPDX-License-Identifier: LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Created: 2026-10-10
# Fails on a declared test with no executable in bin/ and on an executable in bin/ that is not declared.
#
# Usage: check_tests.sh <tests/CMakeLists.txt> <build/tests>
set -eu
cmake_file="$1"
root="$2"
fail=0
declared=$(mktemp)
present=$(mktemp)
trap 'rm -f "$declared" "$present"' EXIT

# One line of the shape project_test(<category> <name> ...) per test; the
# fixed prefix of a line is all that is read.
sed -n 's/^[[:space:]]*project_test([[:space:]]*\([a-z]*\)[[:space:]]\{1,\}\([A-Za-z0-9_.-]*\).*/\1\/bin\/\2/p' \
    "$cmake_file" | sort > "$declared"

if [ ! -s "$declared" ]; then
    echo "check_tests: no project_test() line found in $cmake_file" >&2
    exit 1
fi

( cd "$root" && find . -mindepth 3 -maxdepth 3 -path './*/bin/*' -type f -perm -u+x \
    | sed 's#^\./##' ) | sort > "$present"

while read -r t; do
    [ -x "$root/$t" ] || { echo "check_tests: declared but not built: $t" >&2; fail=1; }
done < "$declared"

while read -r t; do
    grep -qx "$t" "$declared" || { echo "check_tests: in bin/ but not declared: $t" >&2; fail=1; }
done < "$present"

[ "$fail" -eq 0 ] && echo "check_tests: $(wc -l < "$declared") tests declared and present"
exit "$fail"
