#!/bin/sh
# make check runs the Python tests as plain python3, which may have no numpy or scipy: the
# scorecard tests that need them must skip, not fail (#156 left one class unguarded; CI has
# numpy, so it passed there). This runs them in an empty venv, which has neither.
# usage: sh tests/test_python_without_numpy.sh   (make check-patches runs it)
set -u
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
run=0 failed=0
ok()   { run=$((run + 1)); echo "ok     $1"; }
fail() { run=$((run + 1)); failed=$((failed + 1)); echo "not ok $1"; }
ROOT=$(cd "$(dirname "$0")/.." && pwd -P)

python3 -m venv --without-pip "$T/venv" || { echo "not ok making an empty venv"; exit 1; }
PY="$T/venv/bin/python -B -X pycache_prefix=$T/no-pycache"
$PY -c 'import numpy' 2>/dev/null \
    && fail "the venv has no numpy (the setup reproduces a machine without it)" \
    || ok "the venv has no numpy (the setup reproduces a machine without it)"
(cd "$ROOT" && $PY -m unittest discover -s tests -p 'test_*.py') >"$T/log" 2>&1 \
    && ok "the Python tests pass without numpy (those that need it skip)" \
    || { fail "the Python tests pass without numpy (those that need it skip)"; tail -20 "$T/log"; }

echo
echo "$run tests, $failed failed"
[ "$failed" -eq 0 ]
