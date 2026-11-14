#!/bin/sh
# gate_cpu.sh - the no-sound gate's CPU rows (#140): fm1-emu strict timing of TREE's CPU-test module,
# step F playing the full patch plain, with sync (SYNC 1), the ring (X-MOD) and both (SYNC 2), each as
# built and with 3 KB of padding ahead of the engine (tools/gate_pad.c), so a change that only moves
# code shows up as one; and, where TREE has it, "ui": plain with FM1-8P's VCF page redrawing every
# frame (firmware/fm1_8p_cpu_ui, #47), the F budget with the UI drawing. Device ~= emu x 1.08 (FM-1,
# 2026-11-12).
#
# usage: tools/gate_cpu.sh TREE OUT      (prints one line per mode: 8 voices, U and F, code bytes)
# Inputs, as tools/fm1-build.sh takes them: JIELI_TOOLCHAIN, AC79_SDK, FM1_DOCK, PYTHON (fm1-dock's) and
# FM1_LIVE; or FM1_REF, a folder holding fm1-dock, jieli/toolchain, fw-AC79_AIoT_SDK, fm1-dock-venv and
# fm1-emu/target/release/fm1-live. GATE_CPU_PAD=0 skips the padded layout. Exit 2 when an input is missing.
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
mkdir -p "$OUT" || exit 2

# stage MODE PAD DIR: a copy of TREE in DIR whose step F plays MODE (plain, sync, xmod, ring), padded or not
stage() {
    rm -rf "$3" && mkdir -p "$3" && cp -R "$TREE/." "$3/" && rm -rf "$3/build" || return 1
    case $1 in
        sync) add='    {FM_PARAM_OSC_SYNC, 1},' ;;
        xmod) add='    {FM_PARAM_OSC_RING, 1},' ;;
        ring) add='    {FM_PARAM_OSC_SYNC, 1},\n    {FM_PARAM_OSC_RING, 1},' ;;
        *)    add= ;;
    esac
    if [ -n "$add" ]; then
        grep -q '^    {FM_PARAM_HIGHPASS_STEP, 1},$' "$3/firmware/cpu_test.c" || { echo "gate_cpu: step F's patch not found" >&2; return 1; }
        sed -i "s|^    {FM_PARAM_HIGHPASS_STEP, 1},\$|    {FM_PARAM_HIGHPASS_STEP, 1},\\n$add|" "$3/firmware/cpu_test.c"
    fi
    if [ "$2" = pad ]; then
        cp "$ROOT/tools/gate_pad.c" "$3/engine/aa_pad.c"
        f=$3/firmware/fm1_8p_cpu/fm1_8p_cpu.c
        grep -q '^static int init(const hb_host_t \*h)' "$f" && grep -q '^    H = h;' "$f" || { echo "gate_cpu: pad hook not found" >&2; return 1; }
        sed -i 's|^static int init(const hb_host_t \*h)|int32_t fm1_8p_gate_pad(int32_t x);\nstatic int init(const hb_host_t *h)|; s|^    H = h;|    H = h;\n    if (fm1_8p_gate_pad(3) == 7) return 1;|' "$f"
    fi
}

# run MODE PAD: one emulator run; prints "MODE[,pad]  8 = x %  U = x %  F = x %  overruns  code N B".
# MODE ui is plain with a page of the UI redrawing every frame (emu-ui, firmware/fm1_8p_cpu_ui).
run() {
    t=$1${2:+-$2}; d=$OUT/$t; cmd=emu; [ "$1" = ui ] && cmd=emu-ui
    if [ "${GATE_CPU_PREPARE_ONLY:-0}" = 1 ]; then stage "$1" "$2" "$d/src"; return; fi
    stage "$1" "$2" "$d/src" || return 1
    rm -rf "$d/dock" && cp -a "$FM1_DOCK" "$d/dock" || return 1
    (cd "$d/src" && ulimit -n 8192 2>/dev/null; FM1_DOCK=$d/dock JIELI_TOOLCHAIN=$JIELI_TOOLCHAIN AC79_SDK=$AC79_SDK \
        PYTHON=$PY FM1_LIVE=$FM1_LIVE FM1_EMU_UNPINNED=1 tools/fm1-build.sh $cmd) >"$d/emu.log" 2>&1
    r=$d/src/build/fm1/$cmd-report.md
    [ -f "$r" ] || { echo "$t: no report (see $d/emu.log)"; return 1; }
    printf '%-10s %s %s %s\n' "$t" \
        "$(awk -F'|' '/^\| (8|U \(unison burst\)|F \(full patch\)) \|/ { gsub(/ +$/, "", $2); gsub(/^ +/, "", $2); printf "%s = %s  ", substr($2, 1, 1), $4 }' "$r")" \
        "$(grep -o '[0-9]* overruns' "$r" | head -1)" "$(grep -o 'code [0-9]* B' "$d/emu.log" | head -1)"
}

modes="plain sync xmod ring"
for m in $modes; do
    run "$m" "" &
    [ "${GATE_CPU_PAD:-1}" = 0 ] || run "$m" pad &
done
if [ -d "$TREE/firmware/fm1_8p_cpu_ui" ]; then
    run ui "" &                                 # the UI-active row (#47): no padded twin
else
    printf '%-10s n/a (no firmware/fm1_8p_cpu_ui in this tree)\n' ui
fi
wait
