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

expect_ok   "the CPU-test module builds every engine source" module_covers_engine firmware/fm1_8p_cpu/module.cfg engine
mkdir -p "$T/eng" && : >"$T/eng/a.c" && : >"$T/eng/b.c"
printf 'sources = ../cpu_test.c ../../engine/a.c\n' >"$T/module.cfg"
expect_fail "a module missing an engine source fails" module_covers_engine "$T/module.cfg" "$T/eng"

cat >"$T/emu.txt" <<'LOG'
272 fm1-8p cpu: sweeping 1..8 voices
5274 fm1-8p voices/worst permille 0001001c
10270 fm1-8p voices/worst permille 00020020
35250 fm1-8p voices/worst permille 000801f4
---
active module 1; render: last 3784 ticks (2.7 %), worst 4046 (2.9 %), 0 overruns, 1984 blocks
LOG
emu_report "$T/emu.txt" >"$T/report.md"
expect_ok "emu report has one row per logged voice count" grep -q '^| 1 | 28 | 2.8 % |$' "$T/report.md"
expect_ok "emu report: 2 voices" grep -q '^| 2 | 32 | 3.2 % |$' "$T/report.md"
expect_ok "emu report: 8 voices" grep -q '^| 8 | 500 | 50.0 % |$' "$T/report.md"
expect_ok "emu report keeps the host meter line" grep -q 'worst 4046 (2.9 %), 0 overruns' "$T/report.md"
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
