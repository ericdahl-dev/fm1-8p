#!/bin/sh
# fm1-build.sh - build fm1-8p's FM-1 CPU-test firmware from pinned, verified inputs.
#
# Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
#
# usage: tools/fm1-build.sh COMMAND
#   fetch      download fm1-dock, the JieLi toolchain and the AC79 SDK files into $FM1_DEPS
#              (default build/fm1-deps) and check each against firmware/pins.env
#   build      the CPU test as a .dock (USB deploy) and a solo .fwsc, into build/fm1/
#   canaries   each module in firmware/canary/ breaks an fm1-dock safety rule; its build must fail
#   emu        run the CPU test in fm1-emu's strict timing mode; worst load per voice count
#              into build/fm1/emu-report.md (report only)
#   all        fetch, build, canaries
#
# Already have the inputs? Point at them; they are checked all the same:
#   JIELI_TOOLCHAIN (its directory must be the pinned version), AC79_SDK (a checkout),
#   FM1_DOCK (a clone at the pinned commit), PYTHON (needs Pillow), FM1_LIVE (the pinned fm1-live
#   build; FM1_EMU_UNPINNED=1 takes another, marked in the report). fetch never changes these.
# Downloads need curl, git, tar and xz.

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

# verify_sha256 FILE SHA: fail unless FILE exists and hashes to SHA.
verify_sha256() {
    [ -f "$1" ] || { echo "fm1-build: $1: missing" >&2; return 1; }
    got=$(sha256 "$1")
    [ "$got" = "$2" ] || { echo "fm1-build: $1: sha256 $got, pinned $2" >&2; return 1; }
}

# verify_toolchain_dir PATH DIRNAME: fail unless PATH (after links) is the pinned toolchain directory.
verify_toolchain_dir() {
    real=$(cd "$1" 2>/dev/null && pwd -P) || { echo "fm1-build: no toolchain at $1" >&2; return 1; }
    [ "$(basename "$real")" = "$2" ] ||
        { echo "fm1-build: toolchain $(basename "$real"), pinned $2" >&2; return 1; }
}

# abspath PATH: PATH with its directory resolved (links, ..), for a file or a directory.
abspath() {
    if [ -d "$1" ]; then
        (cd "$1" && pwd -P)
    else
        echo "$(cd "$(dirname "$1")" && pwd -P)/$(basename "$1")"
    fi
}

# stage_module SRC DEST ENGINE_DIR: a copy of module SRC's module.cfg in DEST with every path absolute
# (sources, includes and the file of each `cflags FILE =`),
# its own .c files added, @ENGINE_SOURCES@ replaced by every ENGINE_DIR/*.c and @ENGINE@ by ENGINE_DIR.
# So a new engine file is built with no firmware edit.
stage_module() {
    src=$(abspath "$1") eng=$(abspath "$3")
    mkdir -p "$2" || return 1
    rm -f "$2"/module.cfg
    while IFS= read -r line || [ -n "$line" ]; do
        case "$line" in
            sources\ =*|includes\ =*)
                key=${line%% =*} out=
                [ "$key" = sources ] && for f in "$src"/*.c; do [ -f "$f" ] && out="$out $f"; done
                for tok in ${line#*=}; do
                    case "$tok" in
                        @ENGINE_SOURCES@) for f in "$eng"/*.c; do out="$out $f"; done ;;
                        @ENGINE@) out="$out $eng" ;;
                        /*) out="$out $tok" ;;
                        *) out="$out $(abspath "$src/$tok")" ;;
                    esac
                done
                echo "$key =$out" ;;
            cflags\ =*) echo "$line" ;;
            cflags\ *=*)                         # hbmod's `cflags FILE =`: FILE relative to the module
                file=${line#cflags } file=${file%% =*}
                case "$file" in /*) ;; *) file=$(abspath "$src/$file") ;; esac
                echo "cflags $file =${line#* =}" ;;
            *) echo "$line" ;;
        esac
    done <"$1/module.cfg" >"$2/module.cfg"
}

# emu_report LOG: the CPU test's log lines (count << 16 | worst permille) as a Markdown table, plus
# fm1-dock's own render meter. Fails if no voice count was logged.
# burst_count: the unison burst's count, CT_BURST (FM_MAX_VOICES + 1), read from the sources (run from
# the repo root when sourced).
burst_count() {
    echo $(( $(sed -n 's/^#define FM_MAX_VOICES \([0-9]*\).*/\1/p' "${ROOT:-.}/engine/fm1_8p.h") + 1 ))
}

# full_count: the full patch's count, CT_FULL (CT_BURST + 1): the sweep's last step, so also the number
# of steps.
full_count() {
    echo $(( $(burst_count) + 1 ))
}

# emu_window_ms HOLD: how long fm1-emu runs the CPU test: every step (1..8, U and F) held HOLD blocks
# of 256 frames at 44.1 kHz, plus 2 s for boot and the last log line.
emu_window_ms() {
    echo $(( $(full_count) * $1 * 256 * 1000 / 44100 + 2000 ))
}

emu_report() {
    burst=$(burst_count) full=$(full_count)
    all=$(sed -n 's/.*fm1-8p voices\/worst permille \([0-9a-fA-F]\{8\}\).*/\1/p' "$1")
    [ -n "$all" ] || { echo "fm1-build: no voice counts in $1" >&2; return 1; }
    rows=                                        # the last sweep only: from its last count-1 line on
    for h in $all; do
        [ $((0x$h >> 16)) = 1 ] && rows=
        rows="$rows $h"
    done
    echo "| voices | worst permille | of a 256-frame block |"
    echo "| --- | --- | --- |"
    counts=
    for v in $(seq 1 "$full"); do
        last=
        for h in $rows; do [ $((0x$h >> 16)) = "$v" ] && last=$h; done
        [ -n "$last" ] || continue
        counts="$counts $v "
        pm=$((0x$last & 0xFFFF)) label=$v
        [ "$v" = "$burst" ] && label="U (unison burst)"
        [ "$v" = "$full" ] && label="F (full patch)"
        echo "| $label | $pm | $((pm / 10)).$((pm % 10)) % |"
    done
    echo
    grep 'render: last' "$1" || true
    grep 'fm1-8p ERROR' "$1" || true             # a step that failed to start: not timed
    missing=
    for v in $(seq 1 "$full"); do
        case "$counts" in
            *" $v "*) ;;
            *) if [ "$v" = "$burst" ]; then missing="$missing U"
               elif [ "$v" = "$full" ]; then missing="$missing F"
               else missing="$missing $v"; fi ;;
        esac
    done
    if [ -n "$missing" ]; then
        echo
        echo "**Missing voice counts:$missing** (the run ended early, or the sweep didn't reach them)"
        return 1
    fi
}

# check_module_symbols DIR: the module hbmod linked in DIR (module_02200000.elf) defines nothing beyond
# its own objects (DIR/*.o) except allowed helpers. NM: the toolchain's nm.
check_module_symbols() {
    (
        cd "$1" || exit 1
        tmp=$(mktemp -d) || exit 1
        trap 'rm -rf "$tmp"' EXIT
        "$NM" *.o >"$tmp/own" && "$NM" module_02200000.elf >"$tmp/linked" || exit 1
        awk 'NF == 3 { print $3 }' "$tmp/own" | sort -u >"$tmp/own.n"
        awk 'NF == 3 { print $3 }' "$tmp/linked" | sort -u >"$tmp/linked.n"
        # integer helpers (64-bit and division), memory helpers, and what hbmod's link script defines
        bad=$(comm -23 "$tmp/linked.n" "$tmp/own.n" | grep -Ev \
            '^(__(u?(div|mod|divmod)|mul|ashl|ashr|lshr|cmp|ucmp)(si|di)[34]|memset|memcpy|memmove|hb_module|_hb_.*|\..*)$')
        [ -z "$bad" ] && exit 0
        echo "fm1-build: the module links in symbols outside the allowlist (soft-float, libm or heap?):" $bad >&2
        exit 1
    )
}

# canary_caught LOG EXPECT: the canary's build log shows the check named in EXPECT (a fixed string),
# so a canary that fails for another reason (a typo) doesn't count as caught.
canary_caught() {
    grep -qF "$(cat "$2")" "$1"
}

fetch_fm1_dock() {
    if [ -n "${FM1_DOCK_USER:-}" ]; then        # the user's clone: check it, never change it
        [ "$(git -C "$FM1_DOCK" rev-parse HEAD 2>/dev/null)" = "$FM1_DOCK_SHA" ] && return 0
        echo "fm1-build: FM1_DOCK=$FM1_DOCK is not at fm1-dock $FM1_DOCK_SHA (left as it is)" >&2
        return 1
    fi
    if [ ! -d "$FM1_DOCK/.git" ]; then
        mkdir -p "$FM1_DOCK"
        git -C "$FM1_DOCK" init -q
        git -C "$FM1_DOCK" remote add origin "$FM1_DOCK_REPO"
    fi
    git -C "$FM1_DOCK" fetch -q --depth 1 origin "$FM1_DOCK_SHA"
    git -C "$FM1_DOCK" checkout -q --detach FETCH_HEAD
}

fetch_toolchain() {
    if [ -n "${JIELI_TOOLCHAIN_USER:-}" ]; then  # the user's toolchain: check it, never replace it
        verify_toolchain_dir "$JIELI_TOOLCHAIN" "$JIELI_TOOLCHAIN_DIR" && return 0
        echo "fm1-build: JIELI_TOOLCHAIN=$JIELI_TOOLCHAIN is not $JIELI_TOOLCHAIN_DIR; unset it to download the pinned one" >&2
        return 1
    fi
    verify_toolchain_dir "$JIELI_TOOLCHAIN" "$JIELI_TOOLCHAIN_DIR" 2>/dev/null && return 0
    d=$FM1_DEPS/jieli
    mkdir -p "$d" || return 1
    curl -fL --retry 3 -o "$d/toolchain.tar.xz" "$JIELI_TOOLCHAIN_URL" || return 1
    got=$(sha256 "$d/toolchain.tar.xz")
    if [ "$got" != "$JIELI_TOOLCHAIN_SHA256" ]; then     # the URL serves JieLi's latest, unversioned
        echo "fm1-build: JieLi published a new toolchain (got sha256 $got, pinned $JIELI_TOOLCHAIN_SHA256)." >&2
        echo "fm1-build: review it, then update JIELI_TOOLCHAIN_SHA256 and JIELI_TOOLCHAIN_DIR in firmware/pins.env" >&2
        rm -f "$d/toolchain.tar.xz"
        return 1
    fi
    tar -xJf "$d/toolchain.tar.xz" -C "$d"
    rm "$d/toolchain.tar.xz"
    ln -sfn "$JIELI_TOOLCHAIN_DIR" "$d/toolchain"
}

fetch_sdk() {
    rc=0
    for pin in $AC79_SDK_FILES; do
        rel=${pin%%:*} f=$AC79_SDK/cpu/wl82/tools/${pin%%:*}
        verify_sha256 "$f" "${pin#*:}" 2>/dev/null && continue
        if [ -n "${AC79_SDK_USER:-}" ]; then     # the user's SDK: check it, never write into it
            echo "fm1-build: AC79_SDK=$AC79_SDK: $rel is not the pinned file (left as it is)" >&2
            rc=1
            continue
        fi
        mkdir -p "$(dirname "$f")" || return 1
        curl -fsSL --retry 3 -o "$f.part" "$AC79_SDK_RAW/$AC79_SDK_COMMIT/cpu/wl82/tools/$rel" || return 1
        verify_sha256 "$f.part" "${pin#*:}" || { rm -f "$f.part"; return 1; }
        mv "$f.part" "$f"
    done
    return $rc
}

# fetch_emu: FM1_LIVE = a checked fm1-live: the pinned release (downloaded once, checked every time),
# or the user's FM1_LIVE, which must be that same build unless FM1_EMU_UNPINNED=1.
fetch_emu() {
    if [ -n "${FM1_LIVE:-}" ]; then
        verify_sha256 "$FM1_LIVE" "$FM1_LIVE_SHA256" 2>/dev/null && return 0
        if [ -n "${FM1_EMU_UNPINNED:-}" ]; then
            echo "fm1-build: FM1_LIVE=$FM1_LIVE is not the pinned fm1-live; used anyway (FM1_EMU_UNPINNED)" >&2
            return 0
        fi
        echo "fm1-build: FM1_LIVE=$FM1_LIVE is not the pinned fm1-live (set FM1_EMU_UNPINNED=1 to use it anyway)" >&2
        return 1
    fi
    d=$FM1_DEPS/fm1-emu
    if [ ! -f "$d/$FM1_EMU_DIR/fm1-live" ]; then
        mkdir -p "$d" || return 1
        curl -fL --retry 3 -o "$d/fm1-emu.tar.gz" "$FM1_EMU_URL" || return 1
        verify_sha256 "$d/fm1-emu.tar.gz" "$FM1_EMU_SHA256" || return 1
        tar -xzf "$d/fm1-emu.tar.gz" -C "$d" || return 1
        rm "$d/fm1-emu.tar.gz"
    fi
    verify_sha256 "$d/$FM1_EMU_DIR/fm1-live" "$FM1_LIVE_SHA256" || return 1
    FM1_LIVE=$d/$FM1_EMU_DIR/fm1-live
}

# Every pin, checked before anything is built.
check_inputs() {
    rc=0
    [ "$(git -C "$FM1_DOCK" rev-parse HEAD 2>/dev/null)" = "$FM1_DOCK_SHA" ] ||
        { echo "fm1-build: $FM1_DOCK is not fm1-dock $FM1_DOCK_SHA" >&2; rc=1; }
    verify_toolchain_dir "$JIELI_TOOLCHAIN" "$JIELI_TOOLCHAIN_DIR" || rc=1
    [ -x "$JIELI_TOOLCHAIN/pi32v2/bin/clang" ] || { echo "fm1-build: no clang in $JIELI_TOOLCHAIN" >&2; rc=1; }
    for pin in $AC79_SDK_FILES; do
        verify_sha256 "$AC79_SDK/cpu/wl82/tools/${pin%%:*}" "${pin#*:}" || rc=1
    done
    "$PYTHON" -c 'import PIL' 2>/dev/null || { echo "fm1-build: $PYTHON has no Pillow" >&2; rc=1; }
    return $rc
}

dock() {        # run one of fm1-dock's tools from its checkout
    tool=$1
    shift
    (cd "$FM1_DOCK" && JIELI_TOOLCHAIN=$JIELI_TOOLCHAIN AC79_SDK=$AC79_SDK "$PYTHON" "tools/$tool" "$@")
}

build_firmware() {
    check_inputs
    ulimit -n 8192 2>/dev/null || true         # the parallel compiles open many files
    mkdir -p "$OUT"
    stage_module "$ROOT/firmware/fm1_8p_cpu" "$OUT/modules/fm1_8p_cpu" "$ROOT/engine"
    dock hbmod.py "$OUT/modules/fm1_8p_cpu"
    check_module_symbols "$FM1_DOCK/build/modules/fm1_8p_cpu" || return 1
    dock build.py --modules "$OUT/modules/fm1_8p_cpu" --name fm1-8p-cpu --app-name "FM1-8P CPU" \
        --identity "$FM1_IDENTITY"
    cp "$FM1_DOCK/build/modules/fm1_8p_cpu.dock" "$FM1_DOCK/build/fm1-8p-cpu.fwsc" "$OUT/"
    echo "fm1-build: $OUT/fm1_8p_cpu.dock $OUT/fm1-8p-cpu.fwsc ($FM1_IDENTITY)"
}

run_canaries() {
    check_inputs
    rc=0
    for c in "$ROOT"/firmware/canary/*/; do
        canary=$(basename "$c")
        log=$OUT/canary-$canary.log
        if dock hbmod.py "$c" >"$log" 2>&1; then
            echo "fm1-build: canary $canary built: the check was NOT applied" >&2
            rc=1
        elif canary_caught "$log" "$c/expect"; then
            echo "fm1-build: canary $canary caught: $(cat "$c/expect")"
        else
            echo "fm1-build: canary $canary failed, but not with \"$(cat "$c/expect")\" ($log):" >&2
            tail -5 "$log" >&2
            rc=1
        fi
    done
    return $rc
}

run_emu() {
    check_inputs
    fetch_emu
    ulimit -n 8192 2>/dev/null || true
    stage_module "$ROOT/firmware/fm1_8p_cpu_quick" "$OUT/modules/fm1_8p_cpu_quick" "$ROOT/engine"
    dock build.py --modules "$OUT/modules/fm1_8p_cpu_quick" --name fm1-8p-cpu-quick \
        --app-name "FM1-8P CPU" --identity "$FM1_IDENTITY"
    emu_rc=0      # (no pipefail in sh: the log goes to a file first, so a crash isn't hidden by tee)
    (cd "$FM1_DOCK" && FM1_STRICT=1 FM1_LIVE=$FM1_LIVE "$PYTHON" tools/dock.py emu \
        --fwsc build/fm1-8p-cpu-quick.fwsc --ms "${FM1_EMU_MS:-$(emu_window_ms "$(sed -n 's/.*-DCPU_HOLD_BLOCKS=\([0-9]*\)u.*/\1/p' "$ROOT/firmware/fm1_8p_cpu_quick/module.cfg")")}" --out "$OUT/emu") \
        >"$OUT/emu.log" 2>&1 || emu_rc=$?
    cat "$OUT/emu.log"
    report_rc=0
    emu_report "$OUT/emu.log" >"$OUT/emu-report.md" 2>&1 || report_rc=1
    if ! verify_sha256 "$FM1_LIVE" "$FM1_LIVE_SHA256" 2>/dev/null; then
        printf '\nNote: run with an unpinned fm1-live (%s).\n' "$FM1_LIVE" >>"$OUT/emu-report.md"
    fi
    if [ "$emu_rc" -ne 0 ]; then
        printf '\n**The emulator run failed (exit %s): the table is partial or empty.**\n' "$emu_rc" >>"$OUT/emu-report.md"
    fi
    cat "$OUT/emu-report.md"
    [ "$emu_rc" -eq 0 ] && [ "$report_rc" -eq 0 ]
}

[ -n "${FM1_BUILD_LIB:-}" ] && return 0       # sourced by tests/test_fm1_build.sh

set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/firmware/pins.env"
FM1_DOCK_USER=${FM1_DOCK:+1} AC79_SDK_USER=${AC79_SDK:+1} JIELI_TOOLCHAIN_USER=${JIELI_TOOLCHAIN:+1}
FM1_DEPS=${FM1_DEPS:-$ROOT/build/fm1-deps}
JIELI_TOOLCHAIN=${JIELI_TOOLCHAIN:-$FM1_DEPS/jieli/toolchain}
AC79_SDK=${AC79_SDK:-$FM1_DEPS/ac79-sdk}
FM1_DOCK=${FM1_DOCK:-$FM1_DEPS/fm1-dock}
PYTHON=${PYTHON:-python3}
NM=${NM:-$JIELI_TOOLCHAIN/pi32v2/bin/nm}
OUT=$ROOT/build/fm1
mkdir -p "$OUT"

case "${1:-}" in
    fetch)    fetch_fm1_dock; fetch_toolchain; fetch_sdk; check_inputs ;;
    build)    build_firmware ;;
    canaries) run_canaries ;;
    emu)      run_emu ;;
    all)      fetch_fm1_dock; fetch_toolchain; fetch_sdk; build_firmware; run_canaries ;;
    *)        sed -n '2,20s/^# \{0,1\}//p' "$0" >&2; exit 2 ;;
esac
