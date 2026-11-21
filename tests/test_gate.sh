#!/bin/sh
# Tests for tools/gate.sh, the no-sound gate (#140): renders two trees and compares every clip.
# usage: sh tests/test_gate.sh       (run from the repo root; renders, so it takes minutes: make check-gate)
set -u
# The FM-1 inputs come only where a test passes them: with FM1_REF or FM1_DOCK in the environment, every
# gate run would also build and time the CPU rows, and the clip tests would fail on the machine, not the code.
unset FM1_REF FM1_DOCK
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
# A clip the base doesn't list (a change that adds clips, #61): rendered on HEAD only and reported as new,
# not compared, so the gate still runs against an older base. Here the base lacks the three hpf clips.
rm -rf "${T:?}/older"; cp -R "$T/base" "$T/older"
sed 's/^    yield from hpf_steps()$//' "$T/base/tools/gate_cases.py" >"$T/older/tools/gate_cases.py"
expect_ok "a clip the base doesn't list is new, not a failure" env GATE_COVERAGE=0 sh tools/gate.sh "$T/older" "$T/base"
cp "$T/out" "$T/report-new"            # (its own: the coverage tests below read the first report)
expect_ok "and the report names each new clip" grep -q "^new: hpf-3$" "$T/report-new"
expect_ok "and counts only the clips compared" grep -q "^0 of [0-9]* clips differ (3 new, not compared)$" "$T/report-new"
expect_ok "the other way round, a clip HEAD no longer lists is reported gone" env GATE_COVERAGE=0 sh tools/gate.sh "$T/base" "$T/older"
expect_ok "by name" grep -q "^gone: hpf-3$" "$T/out.last"
# A base whose clip list fails can't make the gate pass with nothing compared: the gate can't run (exit 2).
printf 'import sys\nsys.exit(1)\n' >"$T/older/tools/gate_cases.py"
expect_ok "a base whose clip list fails stops the gate" sh -c 'GATE_COVERAGE=0 sh tools/gate.sh "$1" "$2"; [ $? -eq 2 ]' - "$T/older" "$T/base"
rm -rf "${T:?}/older"
# A clip whose name has a backslash is still a clip both trees list: compared, not new (Codex P3).
for t in bs-base bs-head; do
    rm -rf "${T:?}/$t"; cp -R "$T/base" "$T/$t"
    python3 -I -c 'import sys; s = open(sys.argv[1]).read(); d = "    yield \"demo\", \"demo.mid\", []\n"
open(sys.argv[2], "w").write(s.replace(d, d + "    yield \"back\\\\\\\\slash\", \"demo.mid\", []\n"))' \
        "$T/base/tools/gate_cases.py" "$T/$t/tools/gate_cases.py"
done
expect_ok "a clip named with a backslash" env GATE_COVERAGE=0 sh "$T/bs-head/tools/gate.sh" "$T/bs-base" "$T/bs-head"   # (its list)
cp "$T/out" "$T/report-bs"
expect_fail "is not taken for new" grep -q "^new: " "$T/report-bs"
rm -rf "${T:?}/bs-base" "${T:?}/bs-head"
# A tool the cache key needs that fails (here sha256sum) stops the gate (exit 2): two keys that both read
# "-" would let HEAD reuse BASE's renders and pass anything (Codex P2).
mkdir -p "$T/stub" && printf '#!/bin/sh\nexit 1\n' >"$T/stub/sha256sum" && chmod +x "$T/stub/sha256sum"
expect_ok "a failing hash stops the gate" sh -c 'PATH="$1:$PATH" GATE_DIR="$2" GATE_COVERAGE=0 sh tools/gate.sh "$3" "$4"; [ $? -eq 2 ]' - "$T/stub" "$T/gate-stub" "$T/base" "$T/same"
# CPU rows (fm1-emu): only with the FM-1 inputs; without them the gate says so and still passes.
expect_ok "without the FM-1 inputs the gate skips the CPU rows" env -u FM1_REF -u FM1_DOCK GATE_COVERAGE=0 \
    sh tools/gate.sh "$T/base" "$T/same"
expect_ok "and says the CPU rows were skipped" grep -q "^CPU rows: skipped" "$T/out.last"
# Each CPU row stages its own step F (SYNC 2 adds sync and the ring after the highpass step), at every layout
# of GATE_CPU_LAYOUTS: bytes of never-run code ahead of the engine (0: none, the build that ships).
mkdir -p "$T/fm1/dock" "$T/fm1/tc" "$T/fm1/sdk" && : >"$T/fm1/live"
fm1env() { env JIELI_TOOLCHAIN="$T/fm1/tc" AC79_SDK="$T/fm1/sdk" FM1_DOCK="$T/fm1/dock" FM1_LIVE="$T/fm1/live" "$@"; }
expect_ok "the CPU rows stage without an emulator" fm1env GATE_CPU_PREPARE_ONLY=1 GATE_CPU_LAYOUTS="0 512 2048" \
    sh tools/gate_cpu.sh "$T/base" "$T/cpu"
expect_ok "every row at every layout" sh -c "for r in plain sync xmod ring ui seq plain-hn sync-hn xmod-hn ring-hn ui-hn seq-hn; do for n in 0 512 2048; do test -d '$T/cpu/'\$r@\$n/src || exit 1; done; done"
expect_ok "SYNC 2's step F has sync then the ring" sh -c "grep -A2 '{FM_PARAM_HIGHPASS_STEP, [A-Z0-9_]*},' '$T/cpu/ring@0/src/firmware/cpu_test.c' | tr -d '\\n ' | grep -q 'OSC_SYNC,1},{FM_PARAM_OSC_RING,1}'"
expect_ok "plain's step F is the tree's own" cmp "$T/base/firmware/cpu_test.c" "$T/cpu/plain@0/src/firmware/cpu_test.c"
expect_ok "a layout's dead code is that many bytes, and linked" sh -c "grep -q '\.space 512' '$T/cpu/plain@512/src/engine/aa_pad.c' && grep -q '\.space 2048' '$T/cpu/seq-hn@2048/src/engine/aa_pad.c' && grep -q 'fm1_8p_gate_pad(3)' '$T/cpu/seq-hn@2048/src/firmware/fm1_8p_cpu/fm1_8p_cpu.c'"
expect_fail "and layout 0 has none" sh -c "ls '$T/cpu/plain@0/src/engine/aa_pad.c' '$T/cpu/ui@0/src/engine/aa_pad.c'"
# The high-note rows (#269): each mode again with the chord at notes 115..125 (F plays its first four on
# the device's voices), where the oscillators and the filter cost most, the mode's own step F otherwise;
# none with GATE_CPU_HINOTE=0.
expect_ok "every mode's high-note row has the chord at notes 115..125" sh -c "for m in plain sync xmod ring ui seq; do grep -qx 'static const uint8_t CHORD\[FM_MAX_VOICES\] = {115, 117, 119, 121, 122, 123, 124, 125};   /\* #269: the high notes \*/' '$T/cpu/'\$m'-hn@0/src/firmware/cpu_test.c' || exit 1; done"
expect_ok "GATE_CPU_HINOTE=0 skips them" sh -c "env GATE_CPU_PREPARE_ONLY=1 GATE_CPU_HINOTE=0 GATE_CPU_LAYOUTS=0 JIELI_TOOLCHAIN='$T/fm1/tc' AC79_SDK='$T/fm1/sdk' FM1_DOCK='$T/fm1/dock' FM1_LIVE='$T/fm1/live' sh tools/gate_cpu.sh '$T/base' '$T/cpu0' && test -d '$T/cpu0/plain@0' && ! test -e '$T/cpu0/plain-hn@0'"
expect_ok "and keeps its mode's step F (SYNC 2: sync then the ring)" sh -c "grep -A2 '{FM_PARAM_HIGHPASS_STEP, [A-Z0-9_]*},' '$T/cpu/ring-hn@0/src/firmware/cpu_test.c' | tr -d '\\n ' | grep -q 'OSC_SYNC,1},{FM_PARAM_OSC_RING,1}'"
chord() { grep '^static const uint8_t CHORD' "$1/firmware/cpu_test.c"; }
expect_ok "the normal rows keep the tree's own chord" sh -c 'test -n "$1" && test "$1" = "$2"' - "$(chord "$T/base")" "$(chord "$T/cpu/sync@0/src")"
# The rows, through a stand-in for the build and the emulator (GATE_CPU_BUILD): F is 40 % plus 1 % a KB of
# dead code, so layouts 0 1024 2048 read 40, 41 and 42. It writes fm1-emu's log lines (the records read
# them) and the Markdown report (what main's gate_cpu.sh read), alike.
cat >"$T/build-stub.sh" <<'STUB'
n=$(sed -n 's/.*"\.space \([0-9][0-9]*\)".*/\1/p' engine/aa_pad.c 2>/dev/null); n=${n:-0}
if [ -n "${STUB_BUSY:-}" ]; then mkdir "$STUB_BUSY" 2>/dev/null || : >"$STUB_BUSY.overlap"; fi
mkdir -p build/fm1
pm=$((400 + n * 10 / 1024))
f=$((pm / 10)).$((pm % 10))
fline="| F (full patch) | $pm | $f % |"
[ "${STUB_NO_F:-x}" = "$n" ] && fline=
printf '| voices | worst permille | of a 256-frame block |\n| 8 | 145 | 14.5 %% |\n| U (unison burst) | 239 | 23.9 %% |\n%s\n\nrender: last 1 ticks, worst 1, 0 overruns\n' "$fline" >"build/fm1/$1-report.md"
{
    for v in 1 2 3 4 5 6 7; do printf '100 fm1-8p voices/worst permille %04x%04x\n' "$v" $((v * 18)); done
    printf '100 fm1-8p voices/worst permille %04x%04x\n' 8 145 9 239
    [ "${STUB_NO_F:-x}" = "$n" ] || printf '100 fm1-8p voices/worst permille %04x%04x\n' 10 "$pm"
    echo "active module 1; render: last 1 ticks, worst 1, 0 overruns"
} >"build/fm1/$1.log"
[ "${STUB_FAIL_AT:-x}" = "$n" ] && exit 3
echo "module FM1-8P CPU: code 1000 B"
if [ -n "${STUB_BUSY:-}" ]; then sleep 1; rmdir "$STUB_BUSY"; fi
STUB
rows() { fm1env GATE_CPU_BUILD="$T/build-stub.sh" GATE_CPU_LAYOUTS="0 1024 2048" GATE_CPU_HINOTE=0 "$@" sh tools/gate_cpu.sh "$T/base" "$T/cpu-rows"; }
expect_ok "a row is its F's mean and range over the layouts" rows
cp "$T/out" "$T/rows"
expect_ok "and reads so" grep -q '^plain .*F = 41\.0 % (40\.0\.\.42\.0, spread 2\.0, 3 layouts)' "$T/rows"
expect_ok "every layout's own row is kept, the shipped one marked" sh -c "grep -q '^plain@0 .*F = *40\.0 %.*(shipped)' '$T/cpu-rows/layouts.txt' && grep -q '^seq@2048 .*F = *42\.0 %' '$T/cpu-rows/layouts.txt'"
expect_fail "a mean over GATE_CPU_LIMIT fails the run" rows GATE_CPU_LIMIT=40.5
expect_ok "and says so" grep -q '^plain: F mean 41\.0 % over the limit 40\.5 %' "$T/out.last"
expect_ok "a max over it only warns" rows GATE_CPU_LIMIT=41.5
expect_ok "with its layout" grep -q '^plain: max 42\.0 % over the limit 41\.5 % at layout 2048 (mean ok)' "$T/out.last"
expect_ok "the rows' records are kept, one a layout and one a row" sh -c "grep -c '\"kind\": \"cpu-layout\"' '$T/cpu-rows/rows.jsonl' | grep -qx 18 && grep -c '\"kind\": \"cpu\"' '$T/cpu-rows/rows.jsonl' | grep -qx 6"
expect_ok "a mean over the limit is exit 1" sh -c 'env JIELI_TOOLCHAIN="$1/fm1/tc" AC79_SDK="$1/fm1/sdk" FM1_DOCK="$1/fm1/dock" FM1_LIVE="$1/fm1/live" GATE_CPU_BUILD="$1/build-stub.sh" GATE_CPU_LAYOUTS="0 1024 2048" GATE_CPU_HINOTE=0 GATE_CPU_LIMIT=40.5 sh tools/gate_cpu.sh "$1/base" "$1/cpu-rows"; [ $? -eq 1 ]' - "$T"
expect_fail "a layout whose log has no F fails its row" rows STUB_NO_F=1024
expect_ok "and says so" grep -q "^plain@1024: no F in the log's last sweep" "$T/out.last"
expect_ok "and the row is short of a layout" grep -q '^plain: F at only 2 of 3 layouts$' "$T/out.last"
expect_ok "a layout with no F is exit 2 (could not run), not 1" sh -c 'env JIELI_TOOLCHAIN="$1/fm1/tc" AC79_SDK="$1/fm1/sdk" FM1_DOCK="$1/fm1/dock" FM1_LIVE="$1/fm1/live" GATE_CPU_BUILD="$1/build-stub.sh" GATE_CPU_LAYOUTS="0 1024 2048" GATE_CPU_HINOTE=0 STUB_NO_F=1024 sh tools/gate_cpu.sh "$1/base" "$1/cpu-rows"; [ $? -eq 2 ]' - "$T"
expect_fail "a build that exits non-zero fails its row, report or not" rows STUB_FAIL_AT=2048
expect_ok "and says so" grep -q '^plain@2048: the build failed' "$T/out.last"
expect_ok "a tree without FM_MAX_VOICES can't be read (exit 2)" sh -c 'rm -rf "$1/nomax"; cp -R "$1/base" "$1/nomax" && sed "s/^#define FM_MAX_VOICES /#define FM_MAX_VOICEZ /" "$1/base/engine/fm1_8p.h" >"$1/nomax/engine/fm1_8p.h" && env JIELI_TOOLCHAIN="$1/fm1/tc" AC79_SDK="$1/fm1/sdk" FM1_DOCK="$1/fm1/dock" FM1_LIVE="$1/fm1/live" GATE_CPU_BUILD="$1/build-stub.sh" GATE_CPU_LAYOUTS="0" GATE_CPU_HINOTE=0 sh tools/gate_cpu.sh "$1/nomax" "$1/cpu-nomax" >"$1/nomax.out"; r=$?; rm -rf "$1/nomax" "$1/cpu-nomax"; [ $r -eq 2 ] && grep -q "^plain@0: .*FM_MAX_VOICES defined 0 times" "$1/nomax.out"' - "$T"
expect_fail "GATE_CPU_LAYOUTS must be byte counts" rows GATE_CPU_LAYOUTS="0 1k"
expect_fail "and at least one" rows GATE_CPU_LAYOUTS=" "
expect_fail "each once" rows GATE_CPU_LAYOUTS="0 0 1024"
expect_fail "without leading zeros (00 would not read as the shipped 0)" rows GATE_CPU_LAYOUTS="00 1024"
expect_ok "an @ in OUT's path doesn't move the layouts' names" sh -c 'mkdir -p "$1/at@dir" && env JIELI_TOOLCHAIN="$1/fm1/tc" AC79_SDK="$1/fm1/sdk" FM1_DOCK="$1/fm1/dock" FM1_LIVE="$1/fm1/live" GATE_CPU_BUILD="$1/build-stub.sh" GATE_CPU_LAYOUTS="0 1024 2048" GATE_CPU_HINOTE=0 GATE_CPU_LIMIT=41.5 sh tools/gate_cpu.sh "$1/base" "$1/at@dir/o" | grep -q "^plain: max 42.0 % over the limit 41.5 % at layout 2048 (mean ok)"' - "$T"
expect_ok "a layout's size goes to its .space, not the file's comment" sh -c "grep -q '^/\* gate_pad.c - never-run code' '$T/cpu/plain@512/src/engine/aa_pad.c'"
expect_ok "GATE_CPU_JOBS=1 builds one row at a time" sh -c 'rm -f "$1.overlap"; env STUB_BUSY="$1" sh -c "$2" && ! test -e "$1.overlap"' - "$T/busy" \
    "JIELI_TOOLCHAIN='$T/fm1/tc' AC79_SDK='$T/fm1/sdk' FM1_DOCK='$T/fm1/dock' FM1_LIVE='$T/fm1/live' GATE_CPU_BUILD='$T/build-stub.sh' GATE_CPU_LAYOUTS='0 1024' GATE_CPU_HINOTE=0 GATE_CPU_JOBS=1 sh tools/gate_cpu.sh '$T/base' '$T/cpu-jobs'"
# A row that can't be staged says so in its place, and any failed row fails the run.
cp -R "$T/base" "$T/nochord" && sed '/^static const uint8_t CHORD/d' "$T/base/firmware/cpu_test.c" >"$T/nochord/firmware/cpu_test.c"
expect_fail "a failed row fails gate_cpu" fm1env GATE_CPU_BUILD="$T/build-stub.sh" GATE_CPU_LAYOUTS="0 1024" \
    sh tools/gate_cpu.sh "$T/nochord" "$T/cpu-nochord"
expect_ok "and a row that can't be staged says so" grep -q '^plain-hn@1024: stage failed' "$T/out.last"
rm -rf "${T:?}/nochord" "${T:?}/cpu-nochord"
# The gate keeps CPU rows per tree, gate_cpu.sh and its toggles, and never keeps a failed run's
# (GATE_CPU: a stand-in for gate_cpu.sh).
cat >"$T/cpu-stub.sh" <<'STUB'
echo "plain      F =  1.0 %"
[ "${GATE_CPU_HINOTE:-1}" = 0 ] || echo "plain-hn   F =  2.0 %"
[ -z "${STUB_FAIL:-}" ] || { echo "plain-hn: stage failed"; exit 1; }
STUB
rm -rf "$T/gate/cpu"
stub_gate() { env GATE_CPU="$T/cpu-stub.sh" GATE_COVERAGE=0 FM1_DOCK="$T/fm1/dock" "$@" sh tools/gate.sh "$T/base" "$T/same"; }
expect_ok "a failed CPU run fails the gate (exit 1)" sh -c 'env GATE_CPU="$1/cpu-stub.sh" GATE_COVERAGE=0 FM1_DOCK="$1/fm1/dock" STUB_FAIL=1 sh tools/gate.sh "$1/base" "$1/same"; [ $? -eq 1 ]' - "$T"
expect_ok "with its rows" sh -c "grep -q '^CPU rows: base failed' '$T/out.last' && grep -q '^plain-hn: stage failed' '$T/out.last'"
# gate_cpu's exit 2 (a layout that could not run) fails the gate as its 1 does: never a pass.
printf 'echo "plain: F at only 0 of 4 layouts"\nexit 2\n' >"$T/cpu-stub-2.sh"
expect_ok "a CPU run that could not run (exit 2) fails the gate" sh -c 'rm -rf "$1/gate/cpu"; env GATE_CPU="$1/cpu-stub-2.sh" GATE_COVERAGE=0 FM1_DOCK="$1/fm1/dock" sh tools/gate.sh "$1/base" "$1/same"; r=$?; rm -rf "$1/gate/cpu"; [ $r -eq 1 ]' - "$T"
expect_fail "and not kept" sh -c "ls '$T/gate/cpu/'*/rows.txt"
expect_ok "GATE_CPU_HINOTE=0's rows are kept apart" stub_gate GATE_CPU_HINOTE=0
expect_ok "and another GATE_CPU_LIMIT's" sh -c 'n=$(ls -d "$1"/gate/cpu/*/ | wc -l); env GATE_CPU="$1/cpu-stub.sh" GATE_COVERAGE=0 FM1_DOCK="$1/fm1/dock" GATE_CPU_LIMIT=50 sh tools/gate.sh "$1/base" "$1/same" >/dev/null && test "$(ls -d "$1"/gate/cpu/*/ | wc -l)" -gt "$n"' - "$T"
cp tools/gate_pad.c "$T/gate_pad.c.keep"
printf '/* (an edit) */\n' >>tools/gate_pad.c
expect_ok "and the pad's, once it changes" sh -c 'n=$(ls -d "$1"/gate/cpu/*/ | wc -l); env GATE_CPU="$1/cpu-stub.sh" GATE_COVERAGE=0 FM1_DOCK="$1/fm1/dock" sh tools/gate.sh "$1/base" "$1/same" >/dev/null && test "$(ls -d "$1"/gate/cpu/*/ | wc -l)" -gt "$n"' - "$T"
cp "$T/gate_pad.c.keep" tools/gate_pad.c
# Only HEAD's CPU rows judge: a failed base row is shown, and the gate still passes when HEAD's pass.
cat >"$T/cpu-stub-base.sh" <<'STUB'
case $1 in *base*) echo "plain: stage failed"; exit 1 ;; esac
echo "plain      F =  1.0 %"
STUB
expect_ok "a base-only CPU failure is shown, not a gate failure" sh -c 'rm -rf "$1/gate/cpu"; env GATE_CPU="$1/cpu-stub-base.sh" GATE_COVERAGE=0 FM1_DOCK="$1/fm1/dock" sh tools/gate.sh "$1/base" "$1/same" >"$1/bo" && grep -q "^CPU rows: base failed" "$1/bo"' - "$T"
expect_ok "and another GATE_CPU_LAYOUTS's" sh -c 'n=$(ls -d "$1"/gate/cpu/*/ | wc -l); env GATE_CPU="$1/cpu-stub.sh" GATE_COVERAGE=0 FM1_DOCK="$1/fm1/dock" GATE_CPU_LAYOUTS="0 512" sh tools/gate.sh "$1/base" "$1/same" >/dev/null && test "$(ls -d "$1"/gate/cpu/*/ | wc -l)" -gt "$n"' - "$T"
expect_ok "from a default run's" stub_gate
expect_ok "which has its high-note rows" grep -q '^plain-hn ' "$T/out.last"
# make gate runs it: BASE is required, HEAD defaults to the commit checked out.
expect_ok "make gate BASE=... HEAD=... runs the gate" make -s gate BASE="$T/base" HEAD="$T/same" GATE_COVERAGE=0
expect_ok "and reports the clips compared" grep -q "^0 of [0-9]* clips differ" "$T/out.last"
expect_fail "make gate without BASE fails" make -s gate
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
expect_ok "a change in pulse-width modulation fails the gate" caught pwm osc.c pulse_gain_fast 'w ^= 1u << 24;'

echo
echo "$run tests, $failed failed"
[ "$failed" -eq 0 ]
