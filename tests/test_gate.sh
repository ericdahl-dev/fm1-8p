#!/bin/sh
# Tests for tools/gate.sh, the no-sound gate (#140): renders two trees and compares every clip.
# usage: sh tests/test_gate.sh       (run from the repo root; renders, so it takes minutes: make check-gate)
set -u
# On disk under build/, not /tmp (a small tmpfs on some machines): every tree is a full source copy.
mkdir -p build && T=$(mktemp -d "$PWD/build/test-gate.XXXXXX")
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
# caught NAME FILE FUNC STATEMENT: the gate finds a clip that differs on that mutant
caught() {
    mutant "$1" "$2" "$3" "$4" || return 1
    GATE_COVERAGE=0 sh tools/gate.sh "$T/base" "$T/$1"
    [ $? -eq 1 ] && r=0 || r=1          # 1: a clip differs (2 would be the gate failing to run)
    rm -rf "${T:?}/$1"
    return $r
}

tree base
tree same
expect_ok "two identical trees pass" sh tools/gate.sh "$T/base" "$T/same"
cp "$T/out" "$T/report"
expect_ok "and the report says nothing differs" grep -q "^0 of [0-9]* clips differ" "$T/report"
# Coverage: how many clips reach each engine function, so a change can't pass on one clip.
reach() { awk -v f="$1" '$3 == f { print $1 }' "$T/report"; }
expect_ok "the report counts the clips that reach each engine function" grep -q "clips reaching each engine function" "$T/report"
expect_ok "ring_run is reached by more than one clip" test "$(reach ring_run)" -gt 1

# Each path the gate covers: a change that shows only where that path runs must fail it.
PH='p->osc[1].phase += 0x01000000u;'    # osc2 a step out of phase
expect_ok "a change in ring_run (X-MOD, SYNC 2 in chunks) fails the gate" caught ring osc.c ring_run "$PH"
expect_ok "a change in sync_run (SYNC 1 in chunks) fails the gate" caught sync osc.c sync_run "$PH"
expect_ok "a change in the highpass steps fails the gate" caught hpf filter.c fm_filter_set_highpass 'rate_cents += 1200;'
expect_ok "a change in the filter envelope's depth fails the gate" caught fenv voice.c update_cutoff 'fenv += fenv >> 3;'
expect_ok "a change in the DYNAMICS curve fails the gate" caught dyna dyna.c fm_dyna_share_q15 'velocity ^= 1;'
expect_ok "a change in the mix envelope fails the gate" caught mix voice.c update_mix 'env1 += env1 >> 3;'
expect_ok "a change in the chorus fails the gate" caught chorus chorus.c process_on 'in += in >> 3;'
expect_ok "a change in the unison detune fails the gate" caught unison engine.c unison_detune 'if (i == 1) return 0;'
expect_ok "a change in the noise fails the gate" caught noise osc.c noise_next 'p->noise ^= 1u;'
expect_ok "a change in the per-sample special path fails the gate" caught special osc.c next_special "$PH"
expect_ok "a change in the cross-mod depth (FM) fails the gate" caught xmod osc.c fm_osc_pair_set_xmod 'depth += depth >> 2;'
expect_ok "a change in the LFO fails the gate" caught lfo lfo.c fm_lfo_tick 'l->phase += 1u << 28;'
expect_ok "a change in a note's level (velocity) fails the gate" caught level voice.c set_level 'peak -= peak >> 4;'
expect_ok "a change in pitch bend fails the gate" caught bend engine.c fm_engine_pitch_bend 'value ^= 64;'
expect_ok "a change in the controllers (sustain pedal, mod wheel) fails the gate" caught cc engine.c fm_engine_controller 'if (controller == 64 || controller == 1) value = (uint8_t)(127 - value);'

echo
echo "$run tests, $failed failed"
[ "$failed" -eq 0 ]
