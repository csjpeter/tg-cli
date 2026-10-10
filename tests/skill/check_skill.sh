#!/bin/sh
# SPDX-License-Identifier: LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Created: 2026-10-10
# Checks that claude/skills/tg-cli/SKILL.md has its front matter and names only
# commands that the man pages document, and every read command of tg-cli-ro.
set -eu
src="$1"
skill="$src/claude/skills/tg-cli/SKILL.md"
fail=0
err() { echo "check_skill: $*" >&2; fail=1; }

[ "$(sed -n 1p "$skill")" = "---" ] || err "no front matter"
grep -q '^name: tg-cli$' "$skill" || err "front matter lacks 'name: tg-cli'"
grep -q '^description: .*tg-cli-ro' "$skill" || err "description does not name tg-cli-ro"
grep -q 'Do not use' "$skill" || err "description does not say when not to use it"

# Every subcommand used in the skill must be in the man page of the binary.
for c in me dialogs history search contacts user-info watch download; do
    grep -q "^\.B\(I\)\? $c\b" "$src/man/tg-cli-ro.1" || err "man tg-cli-ro lacks $c"
    grep -q "tg-cli-ro $c" "$skill" || err "skill lacks tg-cli-ro $c"
done
for c in send send-file read edit delete forward; do
    grep -q "^\.B\(I\)\? $c\b" "$src/man/tg-cli.1" || err "man tg-cli lacks $c"
    grep -q "tg-cli $c\|echo .* | tg-cli $c" "$skill" || err "skill lacks tg-cli $c"
done
exit $fail
