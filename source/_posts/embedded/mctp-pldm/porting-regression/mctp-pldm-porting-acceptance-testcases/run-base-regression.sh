#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Manual entry point: running this script DOES send I3C management messages.
set -euo pipefail
if (($# != 3)); then
    echo "Usage: bash $0 NETWORK TARGET_EID NEW_LOG_DIR" >&2
    exit 2
fi
net=$1
eid=$2
[[ $net =~ ^[0-9]+$ && ${#net} -le 10 && $eid =~ ^[0-9]+$ && ${#eid} -le 3 ]] || exit 2
((10#$net >= 1 && 10#$net <= 4294967295 && 10#$eid >= 8 && 10#$eid <= 254)) || exit 2
net=$((10#$net))
eid=$((10#$eid))
logdir=$3
mkdir -- "$logdir"  # Refuse to overwrite an earlier run.
bin=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)/pldmtool
{
    date -Iseconds
    uname -a
    sha256sum "$bin"
    printf 'network=%s target_eid=%s expected_base=1.1.0\n' "$net" "$eid"
} > "$logdir/environment.txt"
run() {
    local name=$1
    shift
    printf 'COMMAND:' > "$logdir/$name.log"
    printf ' %q' "$bin" "$@" >> "$logdir/$name.log"
    printf '\n' >> "$logdir/$name.log"
    local rc=0
    "$bin" "$@" >> "$logdir/$name.log" 2>&1 || rc=$?
    printf 'EXIT=%d\n' "$rc" >> "$logdir/$name.log"
    cat "$logdir/$name.log"
    if ((rc)); then
        printf 'FAIL %s exit=%s\n' "$name" "$rc" >> "$logdir/RESULT.txt"
        exit "$rc"
    fi
}
run 01-get-tid base GetTID -N "$net" -m "$eid" --timeout-ms 30000 -v
run 02-get-types base GetPLDMTypes -N "$net" -m "$eid" --timeout-ms 30000 -v
run 03-get-version base GetPLDMVersion -N "$net" -m "$eid" -t base --timeout-ms 30000 -v
grep -q '"Response": "1.1.0"' "$logdir/03-get-version.log" || {
    echo 'STOP: returned Base version differs; choose matching command-version bytes manually' | tee "$logdir/RESULT.txt"
    exit 1
}
run 04-get-commands base GetPLDMCommands -N "$net" -m "$eid" -t base \
    -d 0x00 0xf0 0xf1 0xf1 --timeout-ms 30000 -v
echo 'QUERIES_COMPLETED: compare TID, types and command bitmap with Target; hardware acceptance not inferred' | tee "$logdir/RESULT.txt"
