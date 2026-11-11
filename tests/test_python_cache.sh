#!/bin/sh
# make runs the Python tools as $(PYTHON). That command must read the source, never a stale
# __pycache__/*.pyc, and must not write one. CPython trusts a .pyc whose recorded source mtime
# and size match, so a source rewritten to the same size within the same second (a checkout or
# merge) keeps running the old code: check-patches once failed on a branch whose source was right.
# -B or PYTHONDONTWRITEBYTECODE alone only stop writing; the stale .pyc is still read.
# usage: PYTHON="python3 ..." sh tests/test_python_cache.sh   (make check-patches passes it)
set -u
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
run=0 failed=0
ok()   { run=$((run + 1)); echo "ok     $1"; }
fail() { run=$((run + 1)); failed=$((failed + 1)); echo "not ok $1"; }
PY=${PYTHON:-python3}

# A cache compiled from X = 1, then the source rewritten to X = 2: same size, same mtime.
printf 'X = 1\n' >"$T/m.py"
touch -d @1700000000 "$T/m.py" 2>/dev/null || touch -t 202311142213.20 "$T/m.py"
(cd "$T" && python3 -c 'import m') || { echo "not ok compiling the stale cache"; exit 1; }
printf 'X = 2\n' >"$T/m.py"
touch -d @1700000000 "$T/m.py" 2>/dev/null || touch -t 202311142213.20 "$T/m.py"
[ "$(cd "$T" && python3 -c 'import m; print(m.X)')" = 1 ] \
    && ok "plain python3 reads the stale cache (the setup reproduces the bug)" \
    || fail "plain python3 reads the stale cache (the setup reproduces the bug)"

# $PY reads the source despite the stale cache ...
[ "$(cd "$T" && $PY -c 'import m; print(m.X)')" = 2 ] \
    && ok "make's Python ignores a stale __pycache__" \
    || fail "make's Python ignores a stale __pycache__"

# ... and writes no cache of its own.
mkdir "$T/fresh" && printf 'Y = 3\n' >"$T/fresh/n.py"
(cd "$T/fresh" && $PY -c 'import n') >/dev/null
[ -z "$(find "$T/fresh" -name '*.pyc')" ] \
    && ok "make's Python writes no .pyc" \
    || fail "make's Python writes no .pyc"

echo
echo "$run tests, $failed failed"
[ "$failed" -eq 0 ]
