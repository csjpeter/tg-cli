#!/bin/sh
# SPDX-License-Identifier: LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Proprietary; the owner is the author.
# Created: 2026-10-10
# Checks that LICENSE, the README, the man pages and every file's SPDX tag declare the same proprietary licence.

# Usage: check_license.sh <source root>
set -eu
root="$1"
cd "$root"
fail=0
err() { echo "check_license: $*" >&2; fail=1; }

grep -q '^Proprietary\. The owner is the author\.$' LICENSE || err "LICENSE lacks the proprietary statement"
grep -qi 'general public license' LICENSE && err "LICENSE still contains the GPL text"

grep -q 'Proprietary; the owner is the author' README.md || err "README.md does not state the licence"
grep -n -i 'licen[sc]e: *gpl\|\[GPL' README.md && err "README.md still names the GPL as the project's licence"
for m in man/*.1; do
    grep -q 'Proprietary; the owner is the author' "$m" || err "$m lacks the licence"
    grep -n 'GPL-3.0' "$m" && err "$m names the GPL"
done

# Every tracked source file: the tag is LicenseRef-Proprietary, except the
# vendored code, which keeps its own.
git ls-files '*.c' '*.h' '*.sh' 'CMakeLists.txt' '*.cmake' | while read -r f; do
    case "$f" in src/vendor/*|libs/*|.cache/*) continue ;; esac
    tag=$(sed -n '1,12s/.*SPDX-License-Identifier: *\([^ *]*\).*/\1/p' "$f" | head -1)
    case "$tag" in
        LicenseRef-Proprietary) ;;
        "") ;;   # headers are TG-9's: a file without a tag is not a wrong tag
        *) echo "check_license: $f carries the tag $tag" >&2; echo bad >> "${TMPDIR:-/tmp}/tg-license-$$" ;;
    esac
done
if [ -e "${TMPDIR:-/tmp}/tg-license-$$" ]; then
    rm -f "${TMPDIR:-/tmp}/tg-license-$$"
    fail=1
fi
exit "$fail"
