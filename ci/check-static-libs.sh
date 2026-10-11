#!/bin/bash
# SPDX-License-Identifier: LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Proprietary; the owner is the author.
# Created: 2026-10-11
# Verifies that the binaries carry libtuikit and libptytest statically.

# ci/check-static-libs.sh
#
# The workspace libraries are linked statically (README, "Workspace libraries").
# This script is the runtime confirmation: none of the given binaries may list
# libtuikit.so or libptytest.so among its shared-library dependencies.
#
# Usage:  ./ci/check-static-libs.sh binary...
# Env:    LDD  the ldd program to use (default: ldd); the test overrides it
# Exit:   0 = no workspace shared library is needed
#         1 = one is, or a binary is missing

set -euo pipefail

LDD="${LDD:-ldd}"
status=0

if [ "$#" -eq 0 ]; then
    echo "usage: $0 binary..." >&2
    exit 1
fi

for binary in "$@"; do
    if [ ! -f "$binary" ]; then
        echo "ERROR: binary not found: $binary" >&2
        echo "Run './manage.sh build' first." >&2
        status=1
        continue
    fi
    deps=$("$LDD" "$binary")
    if printf '%s\n' "$deps" | grep -E 'lib(tuikit|ptytest)[^ ]*\.so'; then
        echo "ERROR: $binary depends on a workspace shared library (must be static)" >&2
        status=1
    else
        echo "OK: $binary has no libtuikit.so / libptytest.so dependency"
    fi
done
exit "$status"
