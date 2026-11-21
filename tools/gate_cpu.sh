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
# Inputs, as tools/fm1-build.sh takes them: JIELI_TOOLCHAIN, AC79_SDK, FM1_DOCK, PYTHON (fm1-dock's) and
# FM1_LIVE; or FM1_REF, a folder holding fm1-dock, jieli/toolchain, fw-AC79_AIoT_SDK, fm1-dock-venv and
# fm1-emu/target/release/fm1-live. GATE_CPU_HINOTE=0 skips the high-note rows. Exit 1 when a row failed
# (its line says why) or its mean is over the limit, 2 when an input is missing.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd -P)
TREE=$1 OUT=$2
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
LAYOUTS=${GATE_CPU_LAYOUTS:-0 1024 2048 3072}
LIMIT=${GATE_CPU_LIMIT:-64.8}
JOBS=${GATE_CPU_JOBS:-16}
BUILD=${GATE_CPU_BUILD:-tools/fm1-build.sh}
mkdir -p "$OUT" || exit 2
OUT=$(cd "$OUT" && pwd -P)

# The high-note rows' chord (#269): notes 115..125, of which step F plays 115..121
HN_CHORD='static const uint8_t CHORD[FM_MAX_VOICES] = {115, 117, 119, 121, 122, 123, 124, 125};   /* #269: the high notes */'

# stage MODE BYTES DIR: a copy of TREE in DIR whose step F plays MODE (plain, sync, xmod, ring, ui, seq;
# MODE-hn: its high-note row), with BYTES of never-run code ahead of the engine (0: none)
stage() {
    rm -rf "$3" && mkdir -p "$3" && cp -R "$TREE/." "$3/" && rm -rf "$3/build" || return 1
    case $1 in
        *-hn)
            grep -q '^static const uint8_t CHORD\[FM_MAX_VOICES\] = {[0-9, ]*};' "$3/firmware/cpu_test.c" ||
                { echo "gate_cpu: step F's chord not found" >&2; return 1; }
            sed -i "s|^static const uint8_t CHORD\[FM_MAX_VOICES\] = {[0-9, ]*};.*\$|$HN_CHORD|" "$3/firmware/cpu_test.c" || return 1 ;;
    esac
    case ${1%-hn} in
        sync) add='    {FM_PARAM_OSC_SYNC, 1},' ;;
        xmod) add='    {FM_PARAM_OSC_RING, 1},' ;;
        ring|seq) add='    {FM_PARAM_OSC_SYNC, 1},\n    {FM_PARAM_OSC_RING, 1},' ;;   # (seq: ring too)
        *)    add= ;;
    esac
    if [ -n "$add" ]; then
        # step F's highpass line, whatever its step (1 before #136, the bass boost after): the params go after it
        grep -q '^    {FM_PARAM_HIGHPASS_STEP, [A-Z0-9_]*},$' "$3/firmware/cpu_test.c" || { echo "gate_cpu: step F's patch not found" >&2; return 1; }
        sed -i "s|^\(    {FM_PARAM_HIGHPASS_STEP, [A-Z0-9_]*},\)\$|\1\\n$add|" "$3/firmware/cpu_test.c" || return 1
    fi
    if [ "$2" -gt 0 ]; then
        sed "s/\.space [0-9]*/.space $2/" "$ROOT/tools/gate_pad.c" >"$3/engine/aa_pad.c" || return 1
        f=$3/firmware/fm1_8p_cpu/fm1_8p_cpu.c
        grep -q '^static int init(const hb_host_t \*h)' "$f" && grep -q '^    H = h;' "$f" || { echo "gate_cpu: pad hook not found" >&2; return 1; }
        sed -i 's|^static int init(const hb_host_t \*h)|int32_t fm1_8p_gate_pad(int32_t x);\nstatic int init(const hb_host_t *h)|; s|^    H = h;|    H = h;\n    if (fm1_8p_gate_pad(3) == 7) return 1;|' "$f" || return 1
    fi
}

# run MODE BYTES: one emulator run (MODE may end in -hn) at that layout, its row into OUT/MODE@BYTES.row
# ("MODE@BYTES  8 = x %  U = x %  F = x %  N overruns  code N B"); on stdout only why there is none.
# MODE ui is plain with a page of the UI redrawing every frame (emu-ui, firmware/fm1_8p_cpu_ui).
run() {
    t=$1@$2; d=$OUT/$t; cmd=emu; [ "${1%-hn}" = ui ] && cmd=emu-ui; [ "${1%-hn}" = seq ] && cmd=emu-seq
    rm -f "$d.row"
    if [ "${GATE_CPU_PREPARE_ONLY:-0}" = 1 ]; then stage "$1" "$2" "$d/src"; return; fi
    stage "$1" "$2" "$d/src" || { echo "$t: stage failed (see above)"; return 1; }
    rm -rf "$d/dock" && cp -a "$FM1_DOCK" "$d/dock" || return 1
    case $BUILD in /*) b=$BUILD ;; *) b=$d/src/$BUILD ;; esac
    (cd "$d/src" && ulimit -n 8192 2>/dev/null; FM1_DOCK=$d/dock JIELI_TOOLCHAIN=$JIELI_TOOLCHAIN AC79_SDK=$AC79_SDK \
        PYTHON=$PY FM1_LIVE=$FM1_LIVE FM1_EMU_UNPINNED=1 sh "$b" $cmd) >"$d/emu.log" 2>&1
    r=$d/src/build/fm1/$cmd-report.md
    [ -f "$r" ] || { echo "$t: no report (see $d/emu.log)"; return 1; }
    printf '%-14s %s %s %s\n' "$t" \
        "$(awk -F'|' '/^\| (8|U \(unison burst\)|F \(full patch\)) \|/ { gsub(/ +$/, "", $2); gsub(/^ +/, "", $2); printf "%s = %s  ", substr($2, 1, 1), $4 }' "$r")" \
        "$(grep -o '[0-9]* overruns' "$r" | head -1)" "$(grep -o 'code [0-9]* B' "$d/emu.log" | head -1)" >"$d.row"
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

# each row's figures over its layouts; every layout's own row into OUT/layouts.txt, the shipped one marked
first=${LAYOUTS%% *}
: >"$OUT/layouts.txt"
for R in $rows; do
    for N in $LAYOUTS; do
        [ -f "$OUT/$R@$N.row" ] || continue
        if [ "$N" = 0 ]; then sed 's/$/  (shipped)/' "$OUT/$R@$N.row"; else cat "$OUT/$R@$N.row"; fi >>"$OUT/layouts.txt"
    done
    set --
    for N in $LAYOUTS; do [ -f "$OUT/$R@$N.row" ] && set -- "$@" "$OUT/$R@$N.row"; done
    [ $# -gt 0 ] || continue
    awk -v row="$R" -v limit="$LIMIT" -v first="$OUT/$R@$first.row" '
        function num(s) { sub(/ *%.*/, "", s); return s + 0 }
        {
            n++; split(FILENAME, f, "@"); lay = f[2]; sub(/\.row$/, "", lay)
            for (i = 1; i <= NF; i++) {
                if ($i == "8" && $(i + 1) == "=") e += num($(i + 2))
                if ($i == "U" && $(i + 1) == "=") u += num($(i + 2))
                if ($i == "F" && $(i + 1) == "=") {
                    v = num($(i + 2)); s += v
                    if (n == 1 || v < lo) lo = v
                    if (n == 1 || v > hi) { hi = v; hilay = lay }
                }
                if ($(i + 1) == "overruns") ov += $i
                if ($i == "code" && FILENAME == first) code = $(i + 1)
            }
        }
        END {
            m = s / n
            printf "%-10s 8 = %4.1f %%   U = %4.1f %%   F = %4.1f %% (%.1f..%.1f, spread %.1f, %d layouts)    %d overruns code %s B\n", \
                row, e / n, u / n, m, lo, hi, hi - lo, n, ov, code
            if (m > limit + 0) { printf "%s: F mean %.1f %% over the limit %.1f %%\n", row, m, limit; exit 1 }
            if (hi > limit + 0) printf "%s: max %.1f %% over the limit %.1f %% at layout %s (mean ok)\n", row, hi, limit, hilay
        }' "$@" || rc=1
done
exit $rc
