#!/bin/sh
# SPDX-License-Identifier: LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Proprietary; the owner is the author.
# Created: 2026-10-10
# Fails when a tracked file contains the owner's private domain (policies/private-names-model.md).
#
# Usage: check_private_names.sh <source root>
set -eu
cd "$1"
# The name is built from two parts so that this script does not contain it.
part_a='csasz'
part_b='ar\.em''ail'
pattern="${part_a}${part_b}"

# git grep exits 1 when nothing matches, which is the good case.
if git grep -q -i -e "$pattern" -- . ':!.cache'; then
    echo "check_private_names: the private domain appears in:" >&2
    git grep -l -i -e "$pattern" -- . ':!.cache' >&2
    exit 1
fi
exit 0
