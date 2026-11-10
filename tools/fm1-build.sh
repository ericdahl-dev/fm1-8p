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
#   FM1_DOCK (a clone at the pinned commit), PYTHON (needs Pillow), FM1_LIVE (an fm1-live).
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

# module_covers_engine MODULE_CFG ENGINE_DIR: fail unless the module's sources list every engine .c,
# so a new engine file can't silently drop out of the firmware.
module_covers_engine() {
    srcs=$(sed -n 's/^sources *= *//p' "$1")
    rc=0
    for src in "$2"/*.c; do
        src=$(basename "$src")
        case " $srcs " in
            *"/$src "*) ;;
            *) echo "fm1-build: $1 does not build $src" >&2; rc=1 ;;
        esac
    done
    return $rc
}

# emu_report LOG: the CPU test's log lines (count << 16 | worst permille) as a Markdown table, plus
# fm1-dock's own render meter. Fails if no voice count was logged.
emu_report() {
    rows=$(sed -n 's/.*fm1-8p voices\/worst permille \([0-9a-fA-F]\{8\}\).*/\1/p' "$1")
    [ -n "$rows" ] || { echo "fm1-build: no voice counts in $1" >&2; return 1; }
    echo "| voices | worst permille | of a 256-frame block |"
    echo "| --- | --- | --- |"
    for h in $rows; do
        v=$((0x$h >> 16)) pm=$((0x$h & 0xFFFF))
        echo "| $v | $pm | $((pm / 10)).$((pm % 10)) % |"
    done
    echo
    grep 'render: last' "$1" || true
}

# canary_caught LOG EXPECT: the canary's build log shows the check named in EXPECT (a fixed string),
# so a canary that fails for another reason (a typo) doesn't count as caught.
canary_caught() {
    grep -qF "$(cat "$2")" "$1"
}

fetch_fm1_dock() {
    if [ ! -d "$FM1_DOCK/.git" ]; then
        mkdir -p "$FM1_DOCK"
        git -C "$FM1_DOCK" init -q
        git -C "$FM1_DOCK" remote add origin "$FM1_DOCK_REPO"
    fi
    git -C "$FM1_DOCK" fetch -q --depth 1 origin "$FM1_DOCK_SHA"
    git -C "$FM1_DOCK" checkout -q --detach FETCH_HEAD
}

fetch_toolchain() {
    verify_toolchain_dir "$JIELI_TOOLCHAIN" "$JIELI_TOOLCHAIN_DIR" 2>/dev/null && return 0
    d=$FM1_DEPS/jieli
    mkdir -p "$d"
    curl -fL --retry 3 -o "$d/toolchain.tar.xz" "$JIELI_TOOLCHAIN_URL"
    verify_sha256 "$d/toolchain.tar.xz" "$JIELI_TOOLCHAIN_SHA256"
    tar -xJf "$d/toolchain.tar.xz" -C "$d"
    rm "$d/toolchain.tar.xz"
    ln -sfn "$JIELI_TOOLCHAIN_DIR" "$d/toolchain"
}

fetch_sdk() {
    for pin in $AC79_SDK_FILES; do
        rel=${pin%%:*} f=$AC79_SDK/cpu/wl82/tools/${pin%%:*}
        verify_sha256 "$f" "${pin#*:}" 2>/dev/null && continue
        mkdir -p "$(dirname "$f")"
        curl -fsSL --retry 3 -o "$f" "$AC79_SDK_RAW/$AC79_SDK_COMMIT/cpu/wl82/tools/$rel"
    done
}

fetch_emu() {
    [ -n "${FM1_LIVE:-}" ] && return 0
    d=$FM1_DEPS/fm1-emu
    if [ ! -x "$d/$FM1_EMU_DIR/fm1-live" ]; then
        mkdir -p "$d"
        curl -fL --retry 3 -o "$d/fm1-emu.tar.gz" "$FM1_EMU_URL"
        verify_sha256 "$d/fm1-emu.tar.gz" "$FM1_EMU_SHA256"
        tar -xzf "$d/fm1-emu.tar.gz" -C "$d"
        rm "$d/fm1-emu.tar.gz"
    fi
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
    module_covers_engine "$ROOT/firmware/fm1_8p_cpu/module.cfg" "$ROOT/engine" || rc=1
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
    dock hbmod.py "$ROOT/firmware/fm1_8p_cpu"
    dock build.py --modules "$ROOT/firmware/fm1_8p_cpu" --name fm1-8p-cpu --app-name "FM1-8P CPU" \
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
    mkdir -p "$OUT"
    dock build.py --modules "$ROOT/firmware/fm1_8p_cpu_quick" --name fm1-8p-cpu-quick \
        --app-name "FM1-8P CPU" --identity "$FM1_IDENTITY"
    (cd "$FM1_DOCK" && FM1_STRICT=1 FM1_LIVE=$FM1_LIVE "$PYTHON" tools/dock.py emu \
        --fwsc build/fm1-8p-cpu-quick.fwsc --ms "${FM1_EMU_MS:-5000}" --out "$OUT/emu") | tee "$OUT/emu.log"
    emu_report "$OUT/emu.log" >"$OUT/emu-report.md"
    cat "$OUT/emu-report.md"
}

[ -n "${FM1_BUILD_LIB:-}" ] && return 0       # sourced by tests/test_fm1_build.sh

set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/firmware/pins.env"
FM1_DEPS=${FM1_DEPS:-$ROOT/build/fm1-deps}
JIELI_TOOLCHAIN=${JIELI_TOOLCHAIN:-$FM1_DEPS/jieli/toolchain}
AC79_SDK=${AC79_SDK:-$FM1_DEPS/ac79-sdk}
FM1_DOCK=${FM1_DOCK:-$FM1_DEPS/fm1-dock}
PYTHON=${PYTHON:-python3}
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
