#!/bin/sh
# gate_cpu.sh - the no-sound gate's CPU rows (#140): fm1-emu strict timing of TREE's CPU-test module,
# step F playing the full patch plain, with sync (SYNC 1), the ring (X-MOD) and both (SYNC 2); and,
# where TREE has it, "ui": plain with FM1-8P's VCF page redrawing every
# frame (firmware/fm1_8p_cpu_ui, #47), the F budget with the UI drawing; and "seq": ring with step F
# played by a running sequencer on the device's voices (firmware/fm1_8p_cpu_seq, #36). Device ~= emu x
# 1.08 (FM-1, 2026-11-12). Each mode also runs as MODE-hn, the high-note row (#269), unpadded: the CPU
# test's chord set to notes 115, 117, 119, 121, 122..125, where the oscillators and the filter cost most.
# F plays only the first 4 (CT_FULL_VOICES; seq: the first 4 of its 6-note chord), 115..121; the 8 and U
# columns play all 8, so read the -hn rows' F. GATE_CPU_HINOTE=0 skips them.
#
# Layouts: a row's time moves by points with where its code lands in the flash cache alone (#304), so one
# build's figure misleads. Each row runs at every layout of GATE_CPU_LAYOUTS (default "0 1024 2048 3072":
# bytes of never-run code ahead of the engine, tools/gate_pad.c; 0 is none, the build that ships) and
# reads as its F's mean and range: "plain  8 = x %  U = x %  F = m % (lo..hi, spread s, N layouts)" (8 and
# U as means, overruns summed, the code of the first layout). OUT/layouts.txt keeps every layout's own
# row, the shipped one marked.
# The limit (GATE_CPU_LIMIT, % of a block, default 64.8): a row FAILS when its F MEAN is over it; when
# only a layout's F is, it says "ROW: max x % over the limit y % at layout N (mean ok)", a warning (exit
# 0). Until the device shows whether it swings with layout as the emulator does (#304).
# GATE_CPU_JOBS (default 16) caps the rows building at once; GATE_CPU_BUILD stands in for
# tools/fm1-build.sh (the tests' stub).
#
# usage: tools/gate_cpu.sh TREE OUT      (prints one line per row, MODE-hn rows too: 8 voices, U and F, code bytes)
#        tools/gate_cpu.sh --tools      (the FM-1 tools' identity as JSON, for gate.sh's CPU cache key)
# Inputs, as tools/fm1-build.sh takes them: JIELI_TOOLCHAIN, AC79_SDK, FM1_DOCK, PYTHON (fm1-dock's) and
# FM1_LIVE; or FM1_REF, a folder holding fm1-dock, jieli/toolchain, fw-AC79_AIoT_SDK, fm1-dock-venv and
# fm1-emu/target/release/fm1-live. GATE_CPU_HINOTE=0 skips the high-note rows. Exit 1 when a row's F mean
# is over the limit; 2 when an input is missing, or a layout could not run or has no figures (its line
# says why). The records: OUT/rows.jsonl (tools/gate_record.py), one for each layout and one for each row.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd -P)
TREE=${1:-} OUT=${2:-}
if [ -n "${FM1_REF:-}" ]; then
    : "${JIELI_TOOLCHAIN:=$FM1_REF/jieli/toolchain}" "${AC79_SDK:=$FM1_REF/fw-AC79_AIoT_SDK}"
    : "${FM1_DOCK:=$FM1_REF/fm1-dock}" "${FM1_LIVE:=$FM1_REF/fm1-emu/target/release/fm1-live}"
    [ -n "${FM1_DOCK_PYTHON:-}" ] || FM1_DOCK_PYTHON=$FM1_REF/fm1-dock-venv/bin/python
fi
for v in JIELI_TOOLCHAIN AC79_SDK FM1_DOCK FM1_LIVE; do
    eval "x=\${$v:-}"
    [ -n "$x" ] && [ -e "$x" ] || { echo "gate_cpu: $v is not set or missing" >&2; exit 2; }
done
PY=${FM1_DOCK_PYTHON:-${PYTHON:-python3}}
# --tools: the FM-1 tools' identity, for gate.sh's CPU cache key (tools/gate_run.py fm1-tools), from the
# inputs as resolved above
[ "$TREE" = --tools ] && exec $PY -B "$ROOT/tools/gate_run.py" fm1-tools "$FM1_LIVE" "$JIELI_TOOLCHAIN" "$FM1_DOCK"
[ -n "$TREE" ] && [ -n "$OUT" ] || { echo "usage: tools/gate_cpu.sh TREE OUT | --tools" >&2; exit 2; }
LAYOUTS=${GATE_CPU_LAYOUTS:-0 1024 2048 3072}
LIMIT=${GATE_CPU_LIMIT:-64.8}
JOBS=${GATE_CPU_JOBS:-16}
BUILD=${GATE_CPU_BUILD:-tools/fm1-build.sh}
set -- $LAYOUTS
[ $# -gt 0 ] || { echo "gate_cpu: GATE_CPU_LAYOUTS lists no layout" >&2; exit 2; }
seen=" "
for n in $LAYOUTS; do
    case $n in ''|*[!0-9]*) echo "gate_cpu: GATE_CPU_LAYOUTS: $n is not a byte count" >&2; exit 2 ;; esac
    case $n in 0?*) echo "gate_cpu: GATE_CPU_LAYOUTS: $n has a leading zero (0 alone is the shipped layout)" >&2; exit 2 ;; esac
    case $seen in *" $n "*) echo "gate_cpu: GATE_CPU_LAYOUTS: $n is listed twice" >&2; exit 2 ;; esac
    seen="$seen$n "
done
mkdir -p "$OUT" || exit 2
OUT=$(cd "$OUT" && pwd -P)

# stage MODE BYTES DIR: a copy of TREE in DIR whose step F plays MODE (plain, sync, xmod, ring, ui, seq;
# MODE-hn: its high-note row, the chord at notes 115..125), with BYTES of never-run code ahead of the
# engine (0: none). The edits: tools/gate_stage.py (each line exactly once; macOS's sed can't run them).
stage() {
    rm -rf "$3" && mkdir -p "$3" && cp -R "$TREE/." "$3/" && rm -rf "$3/build" || return 1
    $PY -B "$ROOT/tools/gate_stage.py" "$3" "$1" "$2" "$ROOT/tools/gate_pad.c"
}

# run MODE BYTES: one emulator run (MODE may end in -hn) at that layout, its record (tools/gate_cpu_records.py,
# read from fm1-emu's own log) into OUT/MODE@BYTES.json and its row into OUT/MODE@BYTES.row ("MODE@BYTES
# 8 = x %  U = x %  F = x %  N overruns  code N B"); on stdout only why there is none.
# MODE ui is plain with a page of the UI redrawing every frame (emu-ui, firmware/fm1_8p_cpu_ui).
run() {
    t=$1@$2; d=$OUT/$t; cmd=emu; [ "${1%-hn}" = ui ] && cmd=emu-ui; [ "${1%-hn}" = seq ] && cmd=emu-seq
    rm -f "$d.row" "$d.json"
    if [ "${GATE_CPU_PREPARE_ONLY:-0}" = 1 ]; then stage "$1" "$2" "$d/src"; return; fi
    stage "$1" "$2" "$d/src" || { failed "$1" "$2" "$t: stage failed (see above)"; return 1; }
    rm -rf "$d/dock" && cp -a "$FM1_DOCK" "$d/dock" || { failed "$1" "$2" "$t: cannot copy FM1_DOCK"; return 1; }
    case $BUILD in /*) b=$BUILD ;; *) b=$d/src/$BUILD ;; esac
    (cd "$d/src" && ulimit -n 8192 2>/dev/null; FM1_DOCK=$d/dock JIELI_TOOLCHAIN=$JIELI_TOOLCHAIN AC79_SDK=$AC79_SDK \
        PYTHON=$PY FM1_LIVE=$FM1_LIVE FM1_EMU_UNPINNED=1 sh "$b" $cmd) >"$d/emu.log" 2>&1
    brc=$?                                      # (fm1-build writes a partial report when the emulator fails)
    [ "$brc" -eq 0 ] || { failed "$1" "$2" "$t: the build failed (exit $brc, see $d/emu.log)"; return 1; }
    $PY -B "$ROOT/tools/gate_cpu_records.py" layout "$d/src" "$1" "$2" "$d/src/build/fm1/$cmd.log" "$d/emu.log" "$d"
}

# failed MODE BYTES WHY: a layout that could not run: WHY on stdout, and its record
failed() {
    echo "$3"
    $PY -B "$ROOT/tools/gate_cpu_records.py" error "$1" "$2" "$3" "$OUT/$1@$2" || echo "$1@$2: cannot write its record"
}

# each row and layout runs in the background, at most JOBS at once; any one's failure is the script's
pids= rc=0
bg() {
    set -- $pids
    if [ $# -ge "$JOBS" ]; then wait "$1" || rc=1; shift; pids=$*; fi
    run "$R" "$N" & pids="$pids $!"
}
rows=
for m in plain sync xmod ring ui seq; do
    case $m in
        ui) [ -d "$TREE/firmware/fm1_8p_cpu_ui" ] || { printf '%-10s n/a (no firmware/fm1_8p_cpu_ui in this tree)\n' ui; continue; } ;;
        seq) [ -d "$TREE/firmware/fm1_8p_cpu_seq" ] || { printf '%-10s n/a (no firmware/fm1_8p_cpu_seq in this tree)\n' seq; continue; } ;;
    esac
    rows="$rows $m"
    [ "${GATE_CPU_HINOTE:-1}" = 0 ] || rows="$rows $m-hn"
done
for R in $rows; do
    for N in $LAYOUTS; do bg; done
done
for p in $pids; do wait "$p" || rc=1; done
[ "${GATE_CPU_PREPARE_ONLY:-0}" = 1 ] && exit $rc

# each row over its layouts (tools/gate_cpu_records.py): every layout's own row into OUT/layouts.txt, the
# shipped one marked; every record into OUT/rows.jsonl; the rows' lines on stdout. Its exit is the run's
# when worse: 2 when a layout could not run or has no figures, 1 when a row's F mean is over the limit.
$PY -B "$ROOT/tools/gate_cpu_records.py" rows "$OUT" "$LIMIT" "$LAYOUTS" $rows
r=$?
[ "$r" -gt "$rc" ] && rc=$r
exit $rc
