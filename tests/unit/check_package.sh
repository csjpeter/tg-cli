#!/bin/sh
# SPDX-License-Identifier: LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Proprietary; the owner is the author.
# Created: 2026-10-11
# Builds the Debian package with CPack and checks its metadata and contents.

# Usage: check_package.sh BUILD_DIR WORK_DIR
# The package goes to WORK_DIR (under build/), never to /tmp.
set -eu
build="$1"
work="$2"

mkdir -p "$work"
find "$work" -mindepth 1 -maxdepth 1 -type f -name '*.deb' -delete
( umask 022; cd "$build" && cpack -G DEB --config CPackConfig.cmake \
      -D CPACK_OUTPUT_FILE_PREFIX="$work" )

deb=$(find "$work" -maxdepth 1 -name 'tg-cli_*.deb')
if [ -z "$deb" ] || [ "$(printf '%s\n' "$deb" | wc -l)" -ne 1 ]; then
    echo "FAIL: expected exactly one tg-cli_*.deb in $work, got: '$deb'" >&2
    exit 1
fi

fail=0
check() {
    if ! printf '%s\n' "$2" | grep -q -e "$3"; then
        echo "FAIL: $1: '$3' not found" >&2
        fail=1
    fi
}

control=$(dpkg-deb -f "$deb")
check "control" "$control" '^Package: tg-cli$'
check "control" "$control" '^Version: [0-9][0-9.]*$'
check "control" "$control" '^XB-Git-Commit: [0-9a-f]\{7,\}$'
check "control" "$control" '^XB-Build-Date: [0-9]\{4\}-[0-9][0-9]-[0-9][0-9]T'
check "control" "$control" '^Depends: .*libc6'
check "control" "$control" '^Depends: .*libssl'
if printf '%s\n' "$control" | grep -q 'tuikit\|ptytest'; then
    echo "FAIL: control depends on a workspace library (must be static)" >&2
    fail=1
fi

files=$(dpkg-deb -c "$deb")
for f in usr/bin/tg-cli usr/bin/tg-cli-ro usr/bin/tg-tui \
         usr/share/man/man1/tg-cli.1.gz usr/share/doc/tg-cli/copyright \
         usr/share/claude-skills/tg-cli/SKILL.md; do
    check "contents" "$files" "\./$f\$"
done

exit "$fail"
