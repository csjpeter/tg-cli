#!/bin/sh
# SPDX-License-Identifier: LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Proprietary; the owner is the author.
# Created: 2026-10-11
# The gate time record of manage.sh (policies/manage-sh-model.md, "Every verb
# times itself"): appends one line per verb and prints the Gate line.
#
#   timing.sh record LOG VERB START_EPOCH END_EPOCH RC
#   timing.sh gate LOG        prints "Gate: ..." ; non-zero when not measured

set -eu

case "${1:-}" in
    record)
        [ "$#" -eq 6 ] || { echo "timing.sh record: need LOG VERB START END RC" >&2; exit 2; }
        log="$2"; verb="$3"; start="$4"; end="$5"; rc="$6"
        status=ok
        [ "$rc" -eq 0 ] || status=fail
        mkdir -p "$(dirname "$log")"
        printf '%s %s %ss %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$verb" \
            "$((end - start))" "$status" >> "$log"
        ;;
    gate)
        [ "$#" -eq 2 ] || { echo "timing.sh gate: need LOG" >&2; exit 2; }
        log="$2"
        if [ ! -s "$log" ]; then
            echo "Gate: not measured"
            exit 1
        fi
        # The latest line of each gate verb; install-deb is the install part.
        awk '
            { v = $2; if (v == "install-deb") v = "install"
              if (v == "check" || v == "package" || v == "install") {
                  s = $3; sub(/s$/, "", s); secs[v] = s; st[v] = $4 } }
            END {
                n = split("check package install", order, " ")
                parts = ""; total = 0; found = 0
                for (i = 1; i <= n; i++) {
                    v = order[i]
                    if (v in secs) {
                        parts = parts (found ? ", " : "") v " " secs[v] "s"
                        total += secs[v]; found = 1
                    }
                }
                if (!found) { print "Gate: not measured"; exit 1 }
                printf "Gate: %ds (%s)\n", total, parts
            }' "$log"
        ;;
    *)
        echo "usage: timing.sh record LOG VERB START END RC | gate LOG" >&2
        exit 2
        ;;
esac
