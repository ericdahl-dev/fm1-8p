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
for m in firmware/fm1_8p_cpu firmware/fm1_8p_cpu_quick; do
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

printf '  FAIL  rom_ref: reference to ROM address 0xffc01000\nhbmod: checks failed\n' >"$T/canary.log"
printf 'reference to ROM address\n' >"$T/expect"
expect_ok   "a canary is caught by the check it targets" canary_caught "$T/canary.log" "$T/expect"
printf "rom_ref.c:3: error: unknown type name 'uint32'\n" >"$T/typo.log"
expect_fail "a canary failing for another reason is not caught" canary_caught "$T/typo.log" "$T/expect"
for c in firmware/canary/*/; do
    expect_ok "canary $(basename "$c") names the check it targets" test -s "$c/expect"
done

echo
echo "$run tests, $failed failed"
[ "$failed" -eq 0 ]
