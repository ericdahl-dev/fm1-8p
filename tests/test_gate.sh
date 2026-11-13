#!/bin/sh
# Tests for tools/gate.sh, the no-sound gate (#140): renders two trees and compares every clip.
# usage: sh tests/test_gate.sh       (run from the repo root; renders, so it takes minutes: make check-gate)
set -u
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
run=0 failed=0
ok()   { run=$((run + 1)); echo "ok     $1"; }
fail() { run=$((run + 1)); failed=$((failed + 1)); echo "not ok $1"; }
keep()        { [ -f "$T/out" ] && mv "$T/out" "$T/out.last"; return 0; }
expect_ok()   { t_name=$1; shift; keep; if "$@" >"$T/out" 2>&1; then ok "$t_name"; else fail "$t_name"; tail -20 "$T/out"; fi; }
expect_fail() { t_name=$1; shift; keep; if "$@" >"$T/out" 2>&1; then fail "$t_name"; tail -20 "$T/out"; else ok "$t_name"; fi; }

# tree NAME: a copy of this checkout's sources in $T/NAME (no build output)
tree() {
    mkdir -p "$T/$1"
    for d in Makefile engine host firmware tools data examples tests; do cp -R "$d" "$T/$1/"; done
}
export GATE_DIR="$T/gate"

# mutant NAME FILE FUNC STATEMENT: a copy of the base tree with STATEMENT first in FUNC's body (FILE is
# under engine/): a change that only shows where FUNC runs
mutant() {
    rm -rf "${T:?}/$1"; cp -R "$T/base" "$T/$1"
    python3 -I - "$T/$1/engine/$2" "$3" "$4" <<'PY' || { echo "mutant $1: $3 not found"; return 1; }
import re, sys
path, func, stmt = sys.argv[1:4]
s = open(path).read()
m = re.search(r"^[^\n;]*\b" + re.escape(func) + r"\([^;{]*\)\s*\{\n", s, re.M)
if not m: sys.exit(1)
open(path, "w").write(s[:m.end()] + "    " + stmt + "\n" + s[m.end():])
PY
}
# caught NAME FILE FUNC STATEMENT: the gate fails on that mutant
caught() { mutant "$1" "$2" "$3" "$4" && ! sh tools/gate.sh "$T/base" "$T/$1"; }

tree base
tree same
expect_ok "two identical trees pass" sh tools/gate.sh "$T/base" "$T/same"
expect_ok "and the report says nothing differs" grep -q "^0 of [0-9]* clips differ" "$T/out.last"

# Each path the gate covers: a change that shows only where that path runs must fail it.
PH='p->osc[1].phase += 0x01000000u;'    # osc2 a step out of phase
expect_ok "a change in ring_run (X-MOD, SYNC 2 in chunks) fails the gate" caught ring osc.c ring_run "$PH"
expect_ok "a change in sync_run (SYNC 1 in chunks) fails the gate" caught sync osc.c sync_run "$PH"
expect_ok "a change in the highpass steps fails the gate" caught hpf filter.c fm_filter_set_highpass 'rate_cents += 1200;'
expect_ok "a change in the filter envelope's depth fails the gate" caught fenv voice.c update_cutoff 'fenv += fenv >> 3;'
expect_ok "a change in the DYNAMICS curve fails the gate" caught dyna dyna.c fm_dyna_share_q15 'velocity ^= 1;'

echo
echo "$run tests, $failed failed"
[ "$failed" -eq 0 ]
