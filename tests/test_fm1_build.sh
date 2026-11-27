#!/bin/sh
# Tests for tools/fm1-build.sh's own checks (pins, hashes, module sources, emulator report).
# usage: sh tests/test_fm1_build.sh       (run from the repo root; no network, no toolchain)
set -u
FM1_BUILD_LIB=1 . tools/fm1-build.sh
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
run=0 failed=0
ok()   { run=$((run + 1)); echo "ok     $1"; }
fail() { run=$((run + 1)); failed=$((failed + 1)); echo "not ok $1"; }
expect_ok()   { t_name=$1; shift; if "$@" >"$T/out" 2>&1; then ok "$t_name"; else fail "$t_name"; cat "$T/out"; fi; }
expect_fail() { t_name=$1; shift; if "$@" >"$T/out" 2>&1; then fail "$t_name"; else ok "$t_name"; fi; }

printf 'fm1-8p\n' >"$T/file"
good=$(sha256 "$T/file")
expect_ok   "sha256 of a file matches its known hash" test "$good" = 04f6c86cb753f3bb8b84f00bdaf74efdee260db71f07bff3ffe06432313789d2
expect_ok   "verify_sha256 accepts the right hash" verify_sha256 "$T/file" "$good"
expect_fail "verify_sha256 rejects a wrong hash" verify_sha256 "$T/file" 0000000000000000000000000000000000000000000000000000000000000000
expect_fail "verify_sha256 rejects a missing file" verify_sha256 "$T/none" "$good"

mkdir -p "$T/jieli-linux-toolchains-20260730.1/pi32v2/bin" "$T/jieli-linux-toolchains-20990101.1"
ln -s jieli-linux-toolchains-20260730.1 "$T/toolchain"
expect_ok   "toolchain at the pinned version passes" verify_toolchain_dir "$T/toolchain" jieli-linux-toolchains-20260730.1
expect_fail "toolchain at another version fails" verify_toolchain_dir "$T/jieli-linux-toolchains-20990101.1" jieli-linux-toolchains-20260730.1

# Engine sources are found at build time: a new engine/*.c needs no firmware edit.
T=$(cd "$T" && pwd -P)
mkdir -p "$T/mod/sub" "$T/eng"
: >"$T/mod/shared.c"; : >"$T/mod/sub/own.c"; : >"$T/eng/a.c"; : >"$T/eng/b.c"
printf 'name = x\nsources = ../shared.c @ENGINE_SOURCES@\nincludes = .. @ENGINE@\ncflags = -Os\n' >"$T/mod/sub/module.cfg"
stage_module "$T/mod/sub" "$T/staged/sub" "$T/eng" >/dev/null 2>&1
cfg=$T/staged/sub/module.cfg
srcs=" $(sed -n 's/^sources = //p' "$cfg" 2>/dev/null) "
incs=" $(sed -n 's/^includes = //p' "$cfg" 2>/dev/null) "
has() { case "$1" in *" $2 "*) return 0 ;; esac; return 1; }
expect_ok "staged module builds its own file" has "$srcs" "$T/mod/sub/own.c"
expect_ok "staged module keeps its listed sources" has "$srcs" "$T/mod/shared.c"
expect_ok "staged module builds every engine file" has "$srcs" "$T/eng/b.c"
expect_ok "staged module includes its listed dirs" has "$incs" "$T/mod"
expect_ok "staged module includes the engine" has "$incs" "$T/eng"
expect_ok "staged module keeps its other settings" grep -qx 'cflags = -Os' "$cfg"
: >"$T/eng/new.c"
stage_module "$T/mod/sub" "$T/staged/sub" "$T/eng" >/dev/null 2>&1
expect_ok "a new engine file is built with no firmware edit" has " $(sed -n 's/^sources = //p' "$cfg") " "$T/eng/new.c"
# hbmod's `cflags FILE =` names a file relative to the module, so staging makes it absolute too.
printf 'name = x\nsources = ../shared.c\nincludes = ..\ncflags = -O2\ncflags own.c = -Os\ncflags ../shared.c = -O3\n' >"$T/mod/sub/module.cfg"
stage_module "$T/mod/sub" "$T/staged/sub" "$T/eng" >/dev/null 2>&1
expect_ok "a file's own cflags name its own file absolutely" grep -qx "cflags $T/mod/sub/own.c = -Os" "$cfg"
expect_ok "... and a listed source's" grep -qx "cflags $T/mod/shared.c = -O3" "$cfg"
expect_ok "... and the module's cflags stay" grep -qx 'cflags = -O2' "$cfg"
# FM1_DEBUG=1 builds the debug variant: every cflags line also defines FM1_DEBUG (timing logs).
printf 'name = x\nsources = ../shared.c\ncflags = -O2\ncflags own.c = -Os\n' >"$T/mod/sub/module.cfg"
(FM1_DEBUG=1 stage_module "$T/mod/sub" "$T/staged/sub" "$T/eng") >/dev/null 2>&1   # (a subshell: sh may keep a
#                                                    function call's assignment after it, as macOS's does)
expect_ok "a debug build defines FM1_DEBUG for the module" grep -qx 'cflags = -O2 -DFM1_DEBUG' "$cfg"
expect_ok "... and for a file with its own cflags" grep -qx "cflags $T/mod/sub/own.c = -Os -DFM1_DEBUG" "$cfg"
stage_module "$T/mod/sub" "$T/staged/sub" "$T/eng" >/dev/null 2>&1
expect_ok "a build without it doesn't" grep -qx "cflags $T/mod/sub/own.c = -Os" "$cfg"
(FM1_DEBUG=0 stage_module "$T/mod/sub" "$T/staged/sub" "$T/eng") >/dev/null 2>&1
expect_ok "FM1_DEBUG=0 is not a debug build" grep -qx "cflags $T/mod/sub/own.c = -Os" "$cfg"
# #197: FM1_REVERB picks the reverb a device build compiles (each module's cflags already say
# FM_REVERB_ONE; the default is the FDN4), and FM1_CPU_REVERB the CPU test's reverb mix (0: off), so the
# CPU rows can run each design and each mix; unset, the lines stay as they are
(FM1_REVERB=plate stage_module "$T/mod/sub" "$T/staged/sub" "$T/eng") >/dev/null 2>&1
expect_ok "FM1_REVERB=plate builds the plate" grep -qx 'cflags = -O2 -DFM_REVERB_DESIGN=2' "$cfg"
expect_ok "... in a file with its own cflags too" grep -qx "cflags $T/mod/sub/own.c = -Os -DFM_REVERB_DESIGN=2" "$cfg"
(FM1_REVERB=fdn4 FM1_CPU_REVERB=60 stage_module "$T/mod/sub" "$T/staged/sub" "$T/eng") >/dev/null 2>&1
expect_ok "FM1_REVERB=fdn4 and FM1_CPU_REVERB=60" grep -qx 'cflags = -O2 -DFM_REVERB_DESIGN=1 -DCT_REVERB_MIX=60' "$cfg"
expect_fail "another FM1_REVERB stops it" sh -c "FM1_BUILD_LIB=1 . tools/fm1-build.sh && FM1_REVERB=hall stage_module '$T/mod/sub' '$T/staged/hall' '$T/eng'"
expect_fail "an FM1_CPU_REVERB out of 0..100 stops it" sh -c "FM1_BUILD_LIB=1 . tools/fm1-build.sh && FM1_CPU_REVERB=101 stage_module '$T/mod/sub' '$T/staged/r' '$T/eng'"
for m in fm1_8p fm1_8p_cpu fm1_8p_cpu_quick fm1_8p_cpu_seq fm1_8p_cpu_ui; do
    expect_ok "firmware/$m builds one reverb design" grep -q '^cflags = .*-DFM_REVERB_ONE' "firmware/$m/module.cfg"
done
# hbmod splits each line at its first '=' and strips both sides, so spacing is free.
printf 'name = x\nsources = ../shared.c\ncflags   own.c=-Os\n' >"$T/mod/sub/module.cfg"
stage_module "$T/mod/sub" "$T/staged/sub" "$T/eng" >/dev/null 2>&1
expect_ok "a file's own cflags with other spacing still name it absolutely" grep -qx "cflags $T/mod/sub/own.c = -Os" "$cfg"
# The engine and the CPU test (the render path) build at hbmod's default -O2, which module.cfg doesn't
# restate; the screen and keys stay -Os (#34: -O2 took step F, 6 voices with the filter, from 77.2 % to
# 63.8 % of a block in fm1-emu strict).
for m in firmware/fm1_8p firmware/fm1_8p_cpu firmware/fm1_8p_cpu_quick; do
    case $m in
        firmware/fm1_8p) ui=$(abspath firmware/fm1_8p/fm1_8p.c) ;;
        *) ui=$(abspath firmware/fm1_8p_cpu/fm1_8p_cpu.c) ;;
    esac
    stage_module "$m" "$T/staged/$(basename "$m")" engine >/dev/null 2>&1
    flags=" $(sed -n 's/^cflags = //p' "$T/staged/$(basename "$m")/module.cfg") "
    expect_fail "$m leaves the engine at hbmod's default -O2 (no -O in its cflags)" grep -q -- ' -O' <<EOF2
$flags
EOF2
    expect_ok "... and its screen and keys -Os" grep -qx "cflags $ui = -Os" "$T/staged/$(basename "$m")/module.cfg"
done

# The symbol check runs the pinned toolchain's own nm, so check_inputs requires it.
mkdir -p "$T/tc-nonm/pi32v2/bin"
( JIELI_TOOLCHAIN=$T/tc-nonm JIELI_TOOLCHAIN_DIR=x FM1_DOCK=$T/none FM1_DOCK_SHA=0 AC79_SDK=$T/none AC79_SDK_FILES= PYTHON=false; check_inputs ) >"$T/inputs.out" 2>&1
expect_ok "check_inputs says when the toolchain has no nm" grep -q "no nm in $T/tc-nonm" "$T/inputs.out"

# The linked module may take only integer and memory helpers from the toolchain's libraries: no
# soft-float, libm or heap (hbmod links libcompiler_rt without a word). A stand-in nm prints each
# file's FILE.nm.
mkdir -p "$T/nm/mod"
printf '#!/bin/sh\nfor f; do case "$f" in -*) ;; *) cat "$f.nm" ;; esac; done\n' >"$T/nm/nm"; chmod +x "$T/nm/nm"
: >"$T/nm/mod/module_02200000.elf"; : >"$T/nm/mod/a.o"
printf '02200000 T render\n02200100 t helper\n02200200 T __udivdi3\n02200300 T hb_module\n02200400 t _hb_text_end\n01c04000 T _hb_rt_start\n01c06000 T _hb_rt_end\n01c04010 T fm_osc_pair_render\n01c04010 T fm_osc_pair_next\n01c04010 T fm_filter_process\n01c04010 T fm_chorus_process_block\n01c04010 T fm_out_block\n' >"$T/nm/mod/module_02200000.elf.nm"
printf '00000000 T render\n00000010 t helper\n         U __udivdi3\n' >"$T/nm/mod/a.o.nm"
mkdir -p "$T/nm/tc/pi32v2/lib"                 # the toolchain's libraries, as nm lists them
for l in compiler_rt m c; do : >"$T/nm/tc/pi32v2/lib/lib$l.a"; done
printf 'udivdi3.o:\n00000000 T __udivdi3\naddsf3.o:\n00000000 T __addsf3\n00000000 t local\n' >"$T/nm/tc/pi32v2/lib/libcompiler_rt.a.nm"
printf 'sinf.o:\n00000000 T sinf\n         U __mulsf3\n' >"$T/nm/tc/pi32v2/lib/libm.a.nm"
printf 'malloc.o:\n00000000 T malloc\nmemcpy.o:\n00000000 T memcpy\n' >"$T/nm/tc/pi32v2/lib/libc.a.nm"
mkdir -p "$T/nm/tc/pi32v2/bin" && cp "$T/nm/nm" "$T/nm/tc/pi32v2/bin/nm"
with_nm() { JIELI_TOOLCHAIN="$T/nm/tc" "$@"; }
expect_ok "a module with only integer helpers passes the symbol check" with_nm check_module_symbols "$T/nm/mod"
cp "$T/nm/mod/module_02200000.elf.nm" "$T/nm/clean.nm"
printf '02200500 T __addsf3\n' >>"$T/nm/mod/module_02200000.elf.nm"
expect_fail "a module the link gave a soft-float helper fails it" with_nm check_module_symbols "$T/nm/mod"
# hbmod never cleans build/modules/NAME: an object left from an older build must not hide what the
# link took from the toolchain's libraries.
mkdir -p "$T/nm/stale" && cp "$T/nm/mod/a.o" "$T/nm/mod/a.o.nm" "$T/nm/mod/module_02200000.elf" "$T/nm/stale/"
cp "$T/nm/mod/module_02200000.elf.nm" "$T/nm/stale/"
: >"$T/nm/stale/old.o"; printf '00000000 T __addsf3\n' >"$T/nm/stale/old.o.nm"
expect_fail "a stale object defining a soft-float helper doesn't hide it" with_nm check_module_symbols "$T/nm/stale"
# integer bit helpers (count leading zeros and the like) are integer code too: allowed
mkdir -p "$T/nm/bits" && cp "$T/nm/mod/module_02200000.elf" "$T/nm/bits/"
for h in __clzsi2 __ctzsi2 __popcountsi2 __bswapsi2 __negdi2 __ffssi2 __clzdi2; do
    printf '00000000 T %s\n' "$h" >>"$T/nm/tc/pi32v2/lib/libcompiler_rt.a.nm"
    printf '02200600 T %s\n' "$h" >>"$T/nm/bits/module_02200000.elf.nm"
done
expect_ok "a module linking integer bit helpers (clz, ctz, popcount, ...) passes" with_nm check_module_symbols "$T/nm/bits"
# Only a library's global definitions can be pulled into a link: compiler-local labels that a
# library also has (.L_MergedGlobals, jump tables) are the module's own.
mkdir -p "$T/nm/locals" && cp "$T/nm/mod/module_02200000.elf" "$T/nm/locals/"
cp "$T/nm/clean.nm" "$T/nm/locals/module_02200000.elf.nm"
printf '02200700 d .L_MergedGlobals\n02200800 r .GJTIE2_0_0_\n02200900 t shared_local\n' >>"$T/nm/locals/module_02200000.elf.nm"
printf 'x.o:\n00000000 d .L_MergedGlobals\n00000000 r .GJTIE2_0_0_\n00000000 t shared_local\n' >>"$T/nm/tc/pi32v2/lib/libc.a.nm"
expect_ok "a module whose local labels a library also has locally passes" with_nm check_module_symbols "$T/nm/locals"
# #304: the render loops run from the module's RAM text, away from the flash cache. The check reads the
# linked image, not the cflags: any of the render's entry points the image holds must lie inside its RAM
# text, so dropping FM_RAM_TEXT (or misspelling it) fails the build, and the RAM text stays within its
# budget (FM1_RAM_TEXT_BUDGET, default 12 KiB of fm1-dock's 16).
mkdir -p "$T/rt/mod" && : >"$T/rt/mod/module_02200000.elf"
# rt_syms END [FLASH_ENTRY|none]: RAM text from 01c04000 to END, the entry points at its start; FLASH_ENTRY
# (one of them) at a flash address instead, or "none" for a module that links none of them
rt_syms() {
    printf '01c04000 T _hb_rt_start\n%s T _hb_rt_end\n02200000 T render\n' "$1" >"$T/rt/mod/module_02200000.elf.nm"
    [ "${2:-}" = none ] && return 0
    for f in fm_osc_pair_render fm_osc_pair_next fm_filter_process fm_chorus_process_block fm_out_block; do
        [ "$f" = "${2:-}" ] && printf '02201000 T %s\n' "$f" || printf '01c04010 T %s\n' "$f"
    done >>"$T/rt/mod/module_02200000.elf.nm"
}
rtc() { with_nm check_ram_text "$T/rt/mod"; }
rt_syms 01c06190
expect_ok   "a module with its render entry points in RAM text passes" rtc
rt_syms 01c06190 fm_filter_process
expect_fail "a render entry point left in flash fails" rtc
rt_syms 01c04000 fm_out_block
expect_fail "... and with the RAM text empty (FM_RAM_TEXT dropped from the cflags) too" rtc
rt_syms 01c04000 none
expect_ok   "a module that links none of the render's entry points passes with no RAM text" rtc
rt_syms 01c06190
grep -v ' fm_chorus_process_block$' "$T/rt/mod/module_02200000.elf.nm" >"$T/rt/nm.tmp" && mv "$T/rt/nm.tmp" "$T/rt/mod/module_02200000.elf.nm"
expect_fail "a module missing one entry point of the set fails" rtc
rt_syms 01c06190
grep -v ' fm_osc_pair_next$' "$T/rt/mod/module_02200000.elf.nm" >"$T/rt/nm.tmp" && mv "$T/rt/nm.tmp" "$T/rt/mod/module_02200000.elf.nm"
printf '01c06190 T fm_osc_pair_next\n' >>"$T/rt/mod/module_02200000.elf.nm"
expect_fail "an entry point at the RAM text's end is outside it" rtc
rt_syms 01c06190
grep -v ' fm_out_block$' "$T/rt/mod/module_02200000.elf.nm" >"$T/rt/nm.tmp" && mv "$T/rt/nm.tmp" "$T/rt/mod/module_02200000.elf.nm"
printf '01c03ff0 T fm_out_block\n' >>"$T/rt/mod/module_02200000.elf.nm"
expect_fail "... and one below its start" rtc
rt_syms 01c07001
expect_fail "RAM text over the 12 KiB budget fails" rtc
rt_syms 01c07000
expect_ok   "... and at the budget passes" rtc
rt_syms 01c07001
expect_ok   "FM1_RAM_TEXT_BUDGET moves the budget" env FM1_BUILD_LIB=1 FM1_RAM_TEXT_BUDGET=16384 JIELI_TOOLCHAIN="$T/nm/tc" sh -c '. tools/fm1-build.sh && check_ram_text "$1"' x "$T/rt/mod"
printf '01c04000 T _hb_rt_start\n02200000 T render\n' >"$T/rt/mod/module_02200000.elf.nm"
expect_fail "a module with no RAM text bounds fails" rtc
rt_syms 01c06190 fm_filter_process
expect_fail "with FM1_NM the RAM text check reads the module too (a render entry point in flash fails)" env FM1_BUILD_LIB=1 JIELI_TOOLCHAIN="$T/nm/tc2" FM1_NM="$T/nm/nm" sh -c '. tools/fm1-build.sh && check_ram_text "$1"' x "$T/rt/mod"
rt_syms 01c06190
expect_ok   "... and passes a good one" env FM1_BUILD_LIB=1 JIELI_TOOLCHAIN="$T/nm/tc2" FM1_NM="$T/nm/nm" sh -c '. tools/fm1-build.sh && check_ram_text "$1"' x "$T/rt/mod"
mkdir -p "$T/nm/none" && cp "$T/nm/mod/a.o" "$T/nm/mod/a.o.nm" "$T/nm/none/"
expect_fail "a module with no linked image fails it (nm can't read it)" with_nm check_module_symbols "$T/nm/none"
# FM1_NM: another nm reads the module and the libraries, for a host the toolchain's Linux nm can't run on
# (macOS: its own llvm-nm reads the ELF). Here the toolchain has no nm at all.
mkdir -p "$T/nm/tc2/pi32v2/lib" && cp "$T/nm/tc/pi32v2/lib/"* "$T/nm/tc2/pi32v2/lib/"
with_fm1_nm() { JIELI_TOOLCHAIN="$T/nm/tc2" FM1_NM="$T/nm/nm" "$@"; }
expect_ok "FM1_NM reads a module where the toolchain's nm isn't there" with_fm1_nm check_module_symbols "$T/nm/locals"
expect_fail "with FM1_NM a soft-float helper still fails it" with_fm1_nm check_module_symbols "$T/nm/mod"
# ... and a build runs it on the module it links: fm1-dock's tools stand in, linking the module in
# directory $1 (a clean one builds, so the soft-float one fails on the symbol check alone).
build_with() (
    ROOT=$(pwd) OUT=$T/bout FM1_DOCK=$T/fd JIELI_TOOLCHAIN=$T/nm/tc FM1_IDENTITY=test linked=$1
    check_inputs() { :; }
    dock() {
        case "$1" in
        hbmod.py) d=$FM1_DOCK/build/modules/$(basename "$2"); mkdir -p "$d"
                  cp "$linked/module_02200000.elf" "$linked/module_02200000.elf.nm" "$d/" ;;
        build.py) : >"$FM1_DOCK/build/modules/$(basename "$3").dock"; : >"$FM1_DOCK/build/$5.fwsc" ;;
        esac
    }
    build_firmware
)
mkdir -p "$T/nm/clean" && cp "$T/nm/mod/module_02200000.elf" "$T/nm/clean/" && cp "$T/nm/clean.nm" "$T/nm/clean/module_02200000.elf.nm"
expect_ok   "a build whose module links only integer helpers succeeds" build_with "$T/nm/clean"
expect_fail "a build whose module links a soft-float helper fails" build_with "$T/nm/mod"
mkdir -p "$T/nm/cold" && cp "$T/nm/clean/module_02200000.elf" "$T/nm/cold/"
sed 's/^01c04010 T fm_filter_process$/02201000 T fm_filter_process/' "$T/nm/clean/module_02200000.elf.nm" >"$T/nm/cold/module_02200000.elf.nm"
expect_fail "a build whose module has a render entry point in flash fails (#304)" build_with "$T/nm/cold"
# A compile error: hbmod fails, leaving the last build's clean ELF and .dock in place. The build must
# fail, not check the stale ELF and ship the old .dock.
build_failing() (
    ROOT=$(pwd) OUT=$T/bout FM1_DOCK=$T/fd JIELI_TOOLCHAIN=$T/nm/tc FM1_IDENTITY=test fail=$1
    check_inputs() { :; }
    dock() {
        case "$1" in
        hbmod.py) [ "$(basename "$2")" = "$fail" ] && return 1
                  d=$FM1_DOCK/build/modules/$(basename "$2"); mkdir -p "$d"
                  cp "$T/nm/clean/module_02200000.elf" "$T/nm/clean/module_02200000.elf.nm" "$d/" ;;
        build.py) : >"$FM1_DOCK/build/modules/$(basename "$3").dock"; : >"$FM1_DOCK/build/$5.fwsc" ;;
        esac
    }
    build_firmware
)
build_with "$T/nm/clean" >/dev/null 2>&1      # a good build first: its ELF and .dock stay behind
expect_fail "a build whose synth module fails to compile fails" build_failing fm1_8p
expect_fail "a build whose CPU test fails to compile fails" build_failing fm1_8p_cpu
# The emulator run builds the quick module (the image fm1-emu and CI time) and checks it the same way:
# a soft-float quick module fails before the emulator starts.
emu_with() (
    ROOT=$(pwd) OUT=$T/eout FM1_DOCK=$T/fe JIELI_TOOLCHAIN=$T/nm/tc FM1_IDENTITY=test linked=$1
    FM1_LIVE=$T/live/fm1-live FM1_LIVE_SHA256=0
    PYTHON=$T/emu-ran; printf '#!/bin/sh\n: >"%s.called"\n' "$PYTHON" >"$PYTHON"; chmod +x "$PYTHON"
    check_inputs() { :; }
    fetch_emu() { :; }
    dock() {
        d=$FM1_DOCK/build/modules/fm1_8p_cpu_quick; mkdir -p "$d"
        cp "$linked/module_02200000.elf" "$linked/module_02200000.elf.nm" "$d/"
    }
    mkdir -p "$FM1_DOCK/build"
    run_emu
)
# A canary hbmod builds is still caught when the symbol check refuses it (soft-float that the
# compiler and linker accept): run_canaries checks every canary it builds.
canaries_with() (
    ROOT=$T/croot OUT=$T/cout FM1_DOCK=$T/fc JIELI_TOOLCHAIN=$T/nm/tc linked=$1
    check_inputs() { :; }
    dock() {
        d=$FM1_DOCK/build/modules/$(basename "$2"); mkdir -p "$d"
        cp "$linked/module_02200000.elf" "$linked/module_02200000.elf.nm" "$d/"
    }
    mkdir -p "$OUT" "$ROOT/firmware/canary/float_used"
    printf 'outside the allowlist\n' >"$ROOT/firmware/canary/float_used/expect"
    run_canaries
)
expect_ok   "a canary that links soft-float is caught by the symbol check" canaries_with "$T/nm/mod"
expect_fail "... and one that links only integer helpers is not" canaries_with "$T/nm/clean"
emu_with "$T/nm/clean" >"$T/emu-clean.out" 2>&1
expect_ok "an emulator run whose quick module links only integer helpers starts the emulator" test -e "$T/emu-ran.called"
rm -f "$T/emu-ran.called"
emu_with "$T/nm/mod" >/dev/null 2>&1
expect_ok "an emulator run whose quick module links a soft-float helper never starts the emulator" test ! -e "$T/emu-ran.called"
rm -f "$T/emu-ran.called"
emu_with "$T/nm/cold" >/dev/null 2>&1
expect_ok "... and one whose module has a render entry point in flash never starts it (#304)" test ! -e "$T/emu-ran.called"

for m in firmware/fm1_8p firmware/fm1_8p_cpu firmware/fm1_8p_cpu_quick; do
    expect_ok "$m takes the engine's sources from engine/" grep -q '@ENGINE_SOURCES@' "$m/module.cfg"
done

# fetch never changes inputs the user pointed at, and every pin is checked.
calls=$T/curl.calls
: >"$calls"
curl() {        # stand-in: records the call; -o FILE gets "other"
    echo "$*" >>"$calls"
    while [ $# -gt 0 ]; do [ "$1" = -o ] && printf other >"$2"; shift; done
}
mkdir -p "$T/dock" && git -C "$T/dock" init -q && git -C "$T/dock" -c user.name=t -c user.email=t@t commit -q --allow-empty -m x
head_before=$(git -C "$T/dock" rev-parse HEAD)
FM1_DOCK=$T/dock FM1_DOCK_USER=1 FM1_DOCK_SHA=0000000000000000000000000000000000000000 FM1_DOCK_REPO=$T/none
expect_fail "a user's fm1-dock at another commit is refused" fetch_fm1_dock
expect_ok   "... and left as it was" test "$(git -C "$T/dock" rev-parse HEAD)" = "$head_before"
expect_ok   "... with no remote added" test -z "$(git -C "$T/dock" remote)"

mkdir -p "$T/sdk/cpu/wl82/tools" && printf mine >"$T/sdk/cpu/wl82/tools/uboot.boot"
AC79_SDK=$T/sdk AC79_SDK_USER=1 AC79_SDK_FILES="uboot.boot:$good"
expect_fail "a user's SDK with another file is refused" fetch_sdk
expect_ok   "... without downloading over it" test "$(cat "$T/sdk/cpu/wl82/tools/uboot.boot")" = mine
expect_ok   "... or downloading at all" test ! -s "$calls"

JIELI_TOOLCHAIN=$T/jieli-linux-toolchains-20990101.1 JIELI_TOOLCHAIN_USER=1 JIELI_TOOLCHAIN_DIR=jieli-linux-toolchains-20260730.1
expect_fail "a user's toolchain at another version is refused" fetch_toolchain
expect_ok   "... without downloading" test ! -s "$calls"

FM1_DEPS=$T/deps JIELI_TOOLCHAIN=$T/deps/jieli/toolchain JIELI_TOOLCHAIN_USER= JIELI_TOOLCHAIN_SHA256=$good
JIELI_TOOLCHAIN_URL=http://example.invalid/tc FM1_EMU_URL=http://example.invalid/emu
fetch_toolchain >"$T/tc.out" 2>&1
expect_ok   "a changed toolchain download says JieLi published a new one" grep -q 'JieLi published a new toolchain' "$T/tc.out"

mkdir -p "$T/live" && printf junk >"$T/live/fm1-live" && chmod +x "$T/live/fm1-live"
FM1_LIVE_SHA256=$good FM1_EMU_UNPINNED=
FM1_LIVE=$T/live/fm1-live
expect_fail "a user's fm1-live that isn't the pinned build is refused" fetch_emu
FM1_EMU_UNPINNED=1
expect_ok   "... unless asked for (marked in the report)" fetch_emu
FM1_EMU_UNPINNED= FM1_LIVE=
mkdir -p "$T/deps/fm1-emu/x" && cp "$T/live/fm1-live" "$T/deps/fm1-emu/x/fm1-live"
FM1_EMU_DIR=x FM1_EMU_SHA256=$good
expect_fail "an already-extracted fm1-live is checked too" fetch_emu

{
    echo "272 fm1-8p cpu: sweeping 1..8 voices, then the unison burst"
    for v in 1 2 3 4 5 6 7; do echo "5274 fm1-8p voices/worst permille 000${v}001c"; done
    echo "35250 fm1-8p voices/worst permille 000801f4"
    echo "40250 fm1-8p voices/worst permille 000900eb"
    echo "45250 fm1-8p voices/worst permille 000a0190"
    echo "---"
    echo "active module 1; render: last 3784 ticks (2.7 %), worst 4046 (2.9 %), 0 overruns, 1984 blocks"
} >"$T/emu.txt"
expect_ok "emu report of a full sweep passes" emu_report "$T/emu.txt"
emu_report "$T/emu.txt" >"$T/report.md"
expect_ok "emu report has one row per logged voice count" grep -q '^| 1 | 28 | 2.8 % |$' "$T/report.md"
expect_ok "emu report: 8 voices" grep -q '^| 8 | 500 | 50.0 % |$' "$T/report.md"
expect_ok "emu report: the unison burst is row U" grep -q '^| U (unison burst) | 235 | 23.5 % |$' "$T/report.md"
expect_ok "emu report: the full patch is row F" grep -q '^| F (full patch) | 400 | 40.0 % |$' "$T/report.md"
grep -v 'permille 000a' "$T/emu.txt" >"$T/emu-nofull.txt"
expect_fail "emu report fails when the full patch is missing" emu_report "$T/emu-nofull.txt"
emu_report "$T/emu-nofull.txt" >"$T/report.md" 2>&1
expect_ok "... and names it F" grep -q 'Missing voice counts: F\*\*' "$T/report.md"
{ grep -v 'permille 000a' "$T/emu.txt"; echo "45250 fm1-8p ERROR: step failed to start, not timed; count 0000000a"; } >"$T/emu-err.txt"
expect_fail "emu report fails when a step failed to start" emu_report "$T/emu-err.txt"
emu_report "$T/emu-err.txt" >"$T/report.md" 2>&1
expect_ok "... and quotes the error line" grep -q 'ERROR: step failed to start, not timed; count 0000000a' "$T/report.md"
emu_report "$T/emu.txt" >"$T/report.md"
expect_ok "emu report keeps the host meter line" grep -q 'worst 4046 (2.9 %), 0 overruns' "$T/report.md"
grep -v 'permille 000[4-6]' "$T/emu.txt" >"$T/emu-short.txt"
expect_fail "emu report fails when voice counts are missing" emu_report "$T/emu-short.txt"
emu_report "$T/emu-short.txt" >"$T/report.md" 2>&1
expect_ok "... and names them" grep -q 'Missing voice counts: 4 5 6' "$T/report.md"
grep -v 'permille 0009' "$T/emu.txt" >"$T/emu-noburst.txt"
expect_fail "emu report fails when the unison burst is missing" emu_report "$T/emu-noburst.txt"
emu_report "$T/emu-noburst.txt" >"$T/report.md" 2>&1
expect_ok "... and names it U" grep -q 'Missing voice counts: U\*\*' "$T/report.md"
{   # PLAY pressed during a sweep: the old sweep's rows, then a full new one
    for v in 1 2 3; do echo "1 fm1-8p voices/worst permille 000${v}0063"; done
    cat "$T/emu.txt"
} >"$T/emu-again.txt"
expect_ok "emu report of a restarted sweep passes" emu_report "$T/emu-again.txt"
emu_report "$T/emu-again.txt" >"$T/report.md"
expect_ok "... with the last sweep's rows only" grep -q '^| 1 | 28 | 2.8 % |$' "$T/report.md"
expect_fail "... and none from the earlier one" grep -q '| 99 |' "$T/report.md"
expect_ok "... one row per count" test "$(grep -c '^| [0-9UF]' "$T/report.md")" = 10
expect_ok "the burst's count comes from firmware/cpu_test.h" test "$(burst_count)" = 9
expect_ok "the full patch's count comes from firmware/cpu_test.h" test "$(full_count)" = 10
expect_ok "the emu window covers 10 steps of 86 blocks with a margin" test "$(emu_window_ms 86)" -ge 6700
printf 'nothing here\n' >"$T/empty.txt"
expect_fail "emu report fails when no voice count was logged" emu_report "$T/empty.txt"

# The playable module in fm1-emu: a scripted note sequence must run with no overrun.
{
    echo "260 fm1-8p: ready, voices 00000004"
    echo "---"
    echo "active module 0; render: last 70000 ticks (50.3 %), worst 86000 (61.7 %), 0 overruns, 1200 blocks"
} >"$T/play.txt"
expect_ok "emu play report passes with no overrun" emu_play_report "$T/play.txt"
sed 's/, 0 overruns/, 3 overruns/' "$T/play.txt" >"$T/play-over.txt"
expect_fail "emu play report fails on an overrun" emu_play_report "$T/play-over.txt"
grep -v 'ready' "$T/play.txt" >"$T/play-noinit.txt"
expect_fail "emu play report fails when the module never started" emu_play_report "$T/play-noinit.txt"
{ cat "$T/play.txt"; echo "5000 too slow: stopped 00000000"; } >"$T/play-slow.txt"
expect_fail "emu play report fails when the host stopped the module" emu_play_report "$T/play-slow.txt"
# run_emu_play plays the synth that `build` made: with build/fm1/fm1-8p.fwsc there, nothing is
# rebuilt and the emulator gets that file. fm1-dock's tools and fm1-live stand in.
emu_play_with() (
    ROOT=$(pwd) OUT=$T/epout FM1_DOCK=$T/fp JIELI_TOOLCHAIN=$T/nm/tc FM1_IDENTITY=test
    FM1_LIVE=$T/live/fm1-live FM1_LIVE_SHA256=0
    PYTHON=$T/ep-python; printf '#!/bin/sh\necho "$@" >"%s.args"\n' "$PYTHON" >"$PYTHON"; chmod +x "$PYTHON"
    check_inputs() { :; }
    fetch_emu() { :; }
    dock() { echo "$1" >>"$T/ep-dock.calls"; }
    mkdir -p "$FM1_DOCK" "$OUT"
    run_emu_play
)
mkdir -p "$T/epout" && : >"$T/epout/fm1-8p.fwsc"
: >"$T/ep-dock.calls"
emu_play_with >/dev/null 2>&1
expect_ok "emu-play reuses the built synth: nothing rebuilt" test ! -s "$T/ep-dock.calls"
expect_ok "... and the emulator gets build/fm1's file" grep -q -- "--fwsc $T/epout/fm1-8p.fwsc" "$T/ep-python.args"
expect_ok "an emu-play run with an unpinned fm1-live says so in its report" \
    grep -q 'unpinned fm1-live' "$T/epout/emu-play-report.md"

grep -v 'render: last' "$T/play.txt" >"$T/play-nometer.txt"
emu_play_report "$T/play-nometer.txt" >"$T/play-report.md" 2>&1
expect_ok "emu play report says why when the render meter is missing" grep -q 'No render meter' "$T/play-report.md"

printf '  FAIL  rom_ref: reference to ROM address 0xffc01000\nhbmod: checks failed\n' >"$T/canary.log"
printf 'reference to ROM address\n' >"$T/expect"
expect_ok   "a canary is caught by the check it targets" canary_caught "$T/canary.log" "$T/expect"
printf "rom_ref.c:3: error: unknown type name 'uint32'\n" >"$T/typo.log"
# Downloads retry with backoff: CI's runners see "curl: (35) Connection reset", which curl's own
# --retry doesn't count as transient.
sleeps=$T/sleeps
: >"$sleeps"
sleep() { echo "$1" >>"$sleeps"; }
flaky=$T/flaky
curl() {        # stand-in: fails (35) while $flaky holds a positive count, then writes "fm1-8p\n"
    n=$(cat "$flaky"); echo "$*" >>"$calls"
    if [ "$n" -gt 0 ]; then echo $((n - 1)) >"$flaky"; echo "curl: (35) Recv failure: Connection reset by peer" >&2; return 35; fi
    while [ $# -gt 0 ]; do [ "$1" = -o ] && printf 'fm1-8p\n' >"$2"; shift; done
}
echo 2 >"$flaky"
AC79_SDK=$T/sdk2 AC79_SDK_USER= AC79_SDK_FILES="uboot.boot:$good" AC79_SDK_RAW=http://example.invalid AC79_SDK_COMMIT=x
expect_ok   "an SDK download reset twice is retried until it lands" fetch_sdk
expect_ok   "... with the pinned file in place" verify_sha256 "$T/sdk2/cpu/wl82/tools/uboot.boot" "$good"
expect_ok   "... waiting 2 s, then 4 s" test "$(tr '\n' ' ' <"$sleeps")" = "2 4 "
echo 9 >"$flaky"; : >"$calls"; rm -rf "$T/sdk2"
expect_fail "a download that keeps failing gives up" fetch_sdk
expect_ok   "... after 5 tries" test "$(wc -l <"$calls")" -eq 5
expect_ok   "... leaving no file behind" test ! -e "$T/sdk2/cpu/wl82/tools/uboot.boot"
echo 1 >"$flaky"; : >"$calls"; : >"$sleeps"
(FM1_DEPS=$T/deps3 JIELI_TOOLCHAIN=$T/deps3/jieli/toolchain JIELI_TOOLCHAIN_USER= JIELI_TOOLCHAIN_SHA256=$good
 tar() { :; }; fetch_toolchain) >/dev/null 2>&1
expect_ok   "the toolchain download is retried too" test "$(wc -l <"$calls")" -eq 2
echo 1 >"$flaky"; : >"$calls"
(FM1_DEPS=$T/deps4 FM1_LIVE= FM1_EMU_DIR=x FM1_EMU_SHA256=$good FM1_EMU_URL=http://example.invalid/emu
 tar() { :; }; fetch_emu) >/dev/null 2>&1
expect_ok   "... and fm1-emu's" test "$(wc -l <"$calls")" -eq 2
# fm1-dock: a clone already at the pin (CI's cache) is used as it is; a fetch is retried.
mkdir -p "$T/origin" && git -C "$T/origin" init -q && git -C "$T/origin" -c user.name=t -c user.email=t@t commit -q --allow-empty -m pin
pin=$(git -C "$T/origin" rev-parse HEAD)
git() {         # stand-in for fetches: fails (128) while $flaky holds a positive count
    case " $* " in
        *" fetch "*) echo "git $*" >>"$calls"; n=$(cat "$flaky")
                     if [ "$n" -gt 0 ]; then echo $((n - 1)) >"$flaky"; return 128; fi ;;
    esac
    command git "$@"
}
echo 1 >"$flaky"; : >"$calls"
(FM1_DOCK=$T/dock2 FM1_DOCK_USER= FM1_DOCK_SHA=$pin FM1_DOCK_REPO=$T/origin; fetch_fm1_dock) >/dev/null 2>&1
expect_ok   "an fm1-dock fetch that fails once is retried" test "$(command git -C "$T/dock2" rev-parse HEAD 2>/dev/null)" = "$pin"
: >"$calls"
(FM1_DOCK=$T/dock2 FM1_DOCK_USER= FM1_DOCK_SHA=$pin FM1_DOCK_REPO=$T/none; fetch_fm1_dock) >/dev/null 2>&1
expect_ok   "an fm1-dock clone already at the pin is not fetched again" test ! -s "$calls"
unset -f git
expect_fail "a canary failing for another reason is not caught" canary_caught "$T/typo.log" "$T/expect"
for c in firmware/canary/*/; do
    expect_ok "canary $(basename "$c") names the check it targets" test -s "$c/expect"
done

# release: the packages the Firmware Hub's workflow attests and publishes (#356) are the synth's
# .fwsc and .dock only, never the CPU test's, in a folder of their own (one glob, `build/release/*`).
mkdir -p "$T/built" "$T/rel"
for f in fm1-8p.fwsc fm1_8p.dock fm1-8p-cpu.fwsc fm1_8p_cpu.dock; do printf '%s\n' "$f" >"$T/built/$f"; done
: >"$T/rel/old.fwsc"
expect_ok   "a release stages the synth's packages" stage_release "$T/built" "$T/rel" 0.1
expect_ok   "... exactly the .fwsc and the .dock, named with the version" \
            test "$(LC_ALL=C ls "$T/rel" | tr '\n' ' ')" = "fm1-8p-0.1.fwsc fm1_8p-0.1.dock "
expect_ok   "... as built" cmp -s "$T/built/fm1_8p.dock" "$T/rel/fm1_8p-0.1.dock"
expect_fail "a release with no version fails" stage_release "$T/built" "$T/rel" ""
rm "$T/built/fm1_8p.dock"
expect_fail "a release with a package missing fails" stage_release "$T/built" "$T/rel" 0.1
# The version a tag gives (v0.1 -> 0.1) is what fm1-dock's build.py --release takes, checked before the
# build: X.Y, X.Y.Z or X.Y-suffix, one digit each (the boot log's and OPTIONS > ABOUT's APP_VER).
for v in 0.1 1.0 0.1.2 0.1-beta 0.0-rehearsal1; do
    expect_ok   "release version $v is taken" release_version_ok "$v"
done
for v in "" 10.1 0.1.2.3 0.0.0-rehearsal.1 v0.1 0.1-; do
    expect_fail "release version '$v' is refused" release_version_ok "$v"
done
expect_fail "a release version of two lines is refused" release_version_ok "$(printf '0.1\nzzz')"

echo
echo "$run tests, $failed failed"
[ "$failed" -eq 0 ]
