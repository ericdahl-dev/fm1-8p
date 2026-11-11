#!/bin/sh
# Runs a tinytest binary and passes only if it exits 0 and reports "N tests, 0 failed" with N > 0.
# A link cut short (a killed build) can leave a 0-byte binary, which a shell runs as an empty
# script that exits 0: without these checks, make test would pass without running anything.
# usage: tools/run-tests.sh BIN
if [ $# -ne 1 ] || [ -z "$1" ]; then
    echo "usage: tools/run-tests.sh BIN" >&2
    exit 2
fi
if [ ! -e "$1" ]; then
    echo "run-tests: $1 does not exist; build it with make test" >&2
    exit 1
fi
if [ ! -s "$1" ]; then
    echo "run-tests: $1 is empty; rebuild it (rm $1 && make test)" >&2
    exit 1
fi
dir=$(mktemp -d) || { echo "run-tests: cannot make a temporary directory" >&2; exit 1; }
trap 'rm -rf "$dir"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
# Live through tee; plain sh has no pipefail, so the binary's exit status goes through a file.
{ "$1" 2>&1; echo $? >"$dir/rc"; } | tee "$dir/log"
log=$dir/log
rc=$(cat "$dir/rc")
[ "$rc" -eq 0 ] || exit "$rc"
if ! tail -n 1 "$log" | grep -q '^[1-9][0-9]* tests, 0 failed$'; then
    echo "run-tests: $1 exited 0 without reporting its tests" >&2
    exit 1
fi
