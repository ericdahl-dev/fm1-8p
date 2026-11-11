#!/bin/sh
# Runs a tinytest binary and passes only if it exits 0 and reports "N tests, 0 failed" with N > 0.
# A link cut short (a killed build) can leave a 0-byte binary, which a shell runs as an empty
# script that exits 0: without these checks, make test would pass without running anything.
# usage: tools/run-tests.sh BIN
if [ ! -s "$1" ]; then
    echo "run-tests: $1 is empty; rebuild it (rm $1 && make test)" >&2
    exit 1
fi
log=$(mktemp)
trap 'rm -f "$log"' EXIT
"$1" >"$log" 2>&1
rc=$?
cat "$log"
[ "$rc" -eq 0 ] || exit "$rc"
if ! grep -q '^[1-9][0-9]* tests, 0 failed$' "$log"; then
    echo "run-tests: $1 exited 0 without reporting its tests" >&2
    exit 1
fi
