#!/bin/sh
# Tests for tools/check-engine.sh's own tools (#335): a tool that fails, or lists nothing, stops the check
# (exit 2), never passes it; a real finding is exit 1.
# usage: sh tests/test_check_engine.sh      (run from the repo root; make check-float-free runs it)
set -u
mkdir -p build && T=$(mktemp -d "$PWD/build/test-check-engine.XXXXXX")
trap 'rm -rf "$T"' EXIT
run=0 failed=0
ok()   { run=$((run + 1)); echo "ok     $1"; }
fail() { run=$((run + 1)); failed=$((failed + 1)); echo "not ok $1"; }
# exits NAME WANT CMD...: CMD exits WANT
exits() {
    n=$1 want=$2; shift 2
    "$@" >"$T/out" 2>&1; got=$?
    if [ "$got" -eq "$want" ]; then ok "$n"; else fail "$n (exit $got, want $want)"; tail -5 "$T/out"; fi
}
# stub NAME BODY: a tool on $T/stub/NAME, first on PATH
mkdir -p "$T/stub"
stub() { printf '#!/bin/sh\n%s\n' "$2" >"$T/stub/$1" && chmod +x "$T/stub/$1"; }
SRC=engine/dyna.c       # one small engine source: enough for every layer
CE="sh tools/check-engine.sh"

exits "the engine source passes" 0 $CE "$T/o" $SRC
exits "a canary (heap use) is a finding: exit 1" 1 $CE "$T/o" $SRC tests/canary/heap.c

stub nm 'exit 1'
exits "an nm that fails stops it (exit 2)" 2 env PATH="$T/stub:$PATH" $CE "$T/o" $SRC
grep -q "nm" "$T/out" && ok "and names nm" || fail "and names nm"
stub nm 'exit 0'
exits "an nm that lists nothing stops it (exit 2)" 2 env PATH="$T/stub:$PATH" $CE "$T/o" $SRC
rm -f "$T/stub/nm"

stub objdump 'exit 1'
exits "an objdump that fails stops it (exit 2)" 2 env PATH="$T/stub:$PATH" $CE "$T/o" $SRC
grep -q "objdump" "$T/out" && ok "and names objdump" || fail "and names objdump"
stub objdump 'exit 0'
exits "an objdump that prints nothing stops it (exit 2)" 2 env PATH="$T/stub:$PATH" $CE "$T/o" $SRC
rm -f "$T/stub/objdump"

echo
echo "$run tests, $failed failed"
[ "$failed" -eq 0 ]
