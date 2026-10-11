#!/bin/sh
# SPDX-License-Identifier: LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Proprietary; the owner is the author.
# Created: 2026-10-10
# Fails on a source file or script without the workspace header: licence, copyright holder, creation date, purpose.
#
# Usage: check_headers.sh <source root>
set -eu
cd "$1"
today=$(date +%F)
bad=0
for f in $(git ls-files '*.c' '*.h' '*.sh' 'CMakeLists.txt' '*.cmake'); do
    case "$f" in src/vendor/*|libs/*|.cache/*|build*) continue ;; esac
    head12=$(sed -n '1,12p' "$f")
    problem=""
    echo "$head12" | grep -q 'SPDX-License-Identifier: *LicenseRef-Proprietary' || problem="no SPDX tag"
    echo "$head12" | grep -q 'Copyright (c) [0-9]\{4\} Peter Csaszar' || problem="${problem:+$problem; }no copyright line"
    d=$(echo "$head12" | sed -n 's/.*Created: *\([0-9]\{4\}-[0-9]\{2\}-[0-9]\{2\}\).*/\1/p' | head -1)
    if [ -z "$d" ]; then
        problem="${problem:+$problem; }no Created: date"
    elif [ "$d" \> "$today" ]; then
        problem="${problem:+$problem; }Created: date $d is in the future"
    fi
    # the purpose line is the line after Created:
    n=$(echo "$head12" | grep -n 'Created:' | head -1 | cut -d: -f1)
    if [ -n "$n" ]; then
        purpose=$(echo "$head12" | sed -n "$((n+1))p" | sed 's#^[/*# ]*##; s#[*/ ]*$##')
        [ -n "$purpose" ] || problem="${problem:+$problem; }no purpose line"
    fi
    # no address in a header
    echo "$head12" | grep -q '@[A-Za-z0-9-]*\.[A-Za-z]' && problem="${problem:+$problem; }address in the header"
    if [ -n "$problem" ]; then
        echo "check_headers: $f: $problem" >&2
        bad=1
    fi
done
exit "$bad"
