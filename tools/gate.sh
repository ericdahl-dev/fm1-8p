#!/bin/sh
# gate.sh - the no-sound gate (#140): renders every clip of tools/gate_cases.py on two source trees and
# compares them byte for byte. A change that should not change the sound must pass it.
#
# usage: tools/gate.sh BASE [HEAD]      (make gate BASE=<commit> [HEAD=<commit>])
#   BASE, HEAD   a git commit (or branch) of this repo, or a directory holding a source tree;
#                HEAD defaults to the commit checked out.
#   GATE_DIR     where trees, builds and renders go (default build/gate); a tree's renders are kept,
#                keyed by a hash of its sources, so the next run against the same BASE renders only HEAD.
# Exit status: 0 every clip identical (and HEAD's CPU rows, when they run, pass), 1 some clip differs or a
# CPU row of HEAD failed (a failed layout, or its mean over GATE_CPU_LIMIT; only a layout over it warns),
# 2 the gate could not run.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd -P)
G=${GATE_DIR:-$ROOT/build/gate}
PY=${PYTHON:-python3}   # a command and its flags: used unquoted
mkdir -p "$G" || exit 2
G=$(cd "$G" && pwd -P)

die() { echo "gate: $*" >&2; exit 2; }

# src REF: prints the directory of REF's sources (a directory as given, a commit archived once)
src() {
    if [ -d "$1" ]; then (cd "$1" && pwd -P); return; fi
    c=$(git -C "$ROOT" rev-parse --verify --quiet "$1^{commit}") || die "$1: not a directory or a commit"
    d=$G/src/$c
    if [ ! -f "$d/.complete" ]; then
        rm -rf "$d" && mkdir -p "$d" && git -C "$ROOT" archive "$c" | tar -x -C "$d" || die "$1: cannot archive"
        : >"$d/.complete"
    fi
    echo "$d"
}

# key DIR: a hash of the tree's sources that a render depends on; nothing when it can't be had (no files, a
# hash failing): a pipe would hide that, and two empty keys would let HEAD reuse BASE's renders
key() {
    kl=$(cd "$1" && find Makefile engine host firmware tools data examples -type f ! -path '*/__pycache__/*' |
        LC_ALL=C sort) || return
    [ -n "$kl" ] || return
    ks=$(cd "$1" && printf '%s\n' "$kl" | xargs sha256sum) || return
    ks=$(printf '%s\n' "$ks" | sha256sum) || return
    printf '%s\n' "$ks" | cut -c1-16
}

# need_key DIR: sets k to DIR's key, or stops the gate; hash8 FILE: the first 8 of FILE's hash, or stops it
need_key() { k=$(key "$1"); [ ${#k} -eq 16 ] || die "$1: cannot hash its sources"; }
hash8() {
    h8=$(sha256sum "$1") || die "$1: cannot hash"
    h8=$(printf '%s\n' "$h8" | cut -c1-8); [ ${#h8} -eq 8 ] || die "$1: cannot hash"
}

# renders DIR: renders every clip with DIR's renderer into $G/out/<key> (once per key); prints that folder.
# Only the clips DIR's own tools/gate_cases.py lists, if it has one: an older tree can't render a clip
# added since (its params, its MIDI), and the comparison reports such a clip as new.
renders() {
    need_key "$1"; hash8 "$ROOT/tools/gate_cases.py"; k=$k-$h8; o=$G/out/$k   # (and the list it read)
    if [ ! -f "$o/.complete" ]; then
        rm -rf "$o" && mkdir -p "$o" "$G/build/$k"
        make -s -C "$1" BUILD="$G/build/$k" "$G/build/$k/fm1-8p-render" >"$o/.build.log" 2>&1 ||
            { cat "$o/.build.log" >&2; die "$1: the renderer does not build"; }
        $PY -B "$ROOT/tools/gate_cases.py" "$1" >"$o/.all" || die "$1: cannot list the clips"
        if [ -f "$1/tools/gate_cases.py" ]; then   # (to a file: a pipe would hide its failure)
            $PY -B "$1/tools/gate_cases.py" "$1" >"$o/.own" || die "$1: its gate_cases.py fails"
            awk -F'\t' 'NR == FNR { own[$1] = 1; next } $1 in own' "$o/.own" "$o/.all" >"$o/.cases"
        else
            cp "$o/.all" "$o/.cases"
        fi
        [ -s "$o/.cases" ] || die "$1: no clip to render"   # (else everything is new and nothing compared)
        while IFS='	' read -r name mid args; do
            # shellcheck disable=SC2086 # args are words
            "$G/build/$k/fm1-8p-render" $args "$G/midi/$mid" "$o/$name.wav" >/dev/null 2>"$o/$name.err" ||
                { cat "$o/$name.err" >&2; die "$1: $name does not render"; }
        done <"$o/.cases"
        : >"$o/.complete"
    fi
    echo "$o"
}

[ $# -ge 1 ] || die "usage: gate.sh BASE [HEAD]"
mkdir -p "$G/midi" && cp "$ROOT/examples/demo.mid" "$G/midi/demo.mid" &&
    $PY "$ROOT/tools/gate_midi.py" "$G/midi" || die "cannot write the MIDI"
B=$(src "$1") || exit 2
H=$(src "${2:-HEAD}") || exit 2
OB=$(renders "$B") || exit 2
OH=$(renders "$H") || exit 2

# coverage DIR: renders every clip on a --coverage build of DIR's renderer, one .gcda set per clip,
# and prints how many clips reach each engine function (tools/gate_coverage.py)
coverage() {
    need_key "$1"; c=$G/cov/$k
    rm -rf "$c" && mkdir -p "$c/runs" || die "cannot write $c"
    make -s -C "$1" BUILD="$c/build" CFLAGS="-O0 -g --coverage" "$c/build/fm1-8p-render" >"$c/build.log" 2>&1 ||
        { cat "$c/build.log" >&2; die "$1: the coverage build fails"; }
    while IFS='	' read -r name mid args; do
        mkdir -p "$c/p/$name"
        # shellcheck disable=SC2086
        GCOV_PREFIX="$c/p/$name" GCOV_PREFIX_STRIP=0 "$c/build/fm1-8p-render" $args "$G/midi/$mid" \
            "$c/x.wav" >/dev/null 2>&1 || die "$1: $name does not render (coverage build)"
        mkdir -p "$c/runs/$name" && find "$c/p/$name" -name '*.gcda' -path '*/engine/*' \
            -exec mv {} "$c/runs/$name/" \;
        rm -rf "$c/p/$name"
    done <"$OH/.cases"
    $PY -B "$ROOT/tools/gate_coverage.py" "$c/build/engine" "$1" "$c/runs" || die "coverage failed"
}

[ "${GATE_COVERAGE:-1}" = 0 ] || coverage "$H"

# cpu DIR: the CPU rows of DIR (tools/gate_cpu.sh, or GATE_CPU: a stand-in for tests), kept by DIR's key,
# the script's, its pad's (tools/gate_pad.c) and its toggles (GATE_CPU_LAYOUTS, GATE_CPU_HINOTE,
# GATE_CPU_LIMIT): a row added there or skipped by a toggle must not come back missing. A failed run's rows
# are shown, not kept.
cpu() {
    s=${GATE_CPU:-$ROOT/tools/gate_cpu.sh}
    need_key "$1"; hash8 "$s"; k=$k-$h8; hash8 "$ROOT/tools/gate_pad.c"
    k=$k-$h8-l$(printf "%s" "${GATE_CPU_LAYOUTS:-0 1024 2048 3072}" | tr " " _)h${GATE_CPU_HINOTE:-1}-m${GATE_CPU_LIMIT:-64.8}
    o=$G/cpu/$k
    if [ ! -f "$o/rows.txt" ]; then
        sh "$s" "$1" "$o/runs" >"$o.rows" 2>"$o.err" || { sort "$o.rows"; cat "$o.err" >&2; rm -f "$o.rows" "$o.err"; return 1; }
        mkdir -p "$o" && sort "$o.rows" >"$o/rows.txt" && rm -f "$o.rows" "$o.err"
    fi
    cat "$o/rows.txt"
}

# listed NAME FILE: NAME is a clip of the case list FILE
listed() { n="$1" awk -F'\t' '$1 == ENVIRON["n"] { f = 1 } END { exit !f }' "$2"; }   # (not -v: it unescapes)
n=0 d=0 new=0
while IFS='	' read -r name mid args; do
    if ! listed "$name" "$OB/.cases"; then new=$((new + 1)); echo "new: $name"; continue; fi   # not on the base
    [ -f "$OB/$name.wav" ] || die "$name: listed on the base but not rendered"
    n=$((n + 1))
    if ! cmp -s "$OB/$name.wav" "$OH/$name.wav"; then d=$((d + 1)); echo "differs: $name"; fi
done <"$OH/.cases"
while IFS='	' read -r name mid args; do                # on the base only: say so, nothing to compare
    listed "$name" "$OH/.cases" || echo "gone: $name"
done <"$OB/.cases"
[ "$n" -gt 0 ] || die "no clip on both trees: nothing compared"
if [ "$new" -gt 0 ]; then echo "$d of $n clips differ ($new new, not compared)"; else echo "$d of $n clips differ"; fi

if [ -z "${FM1_REF:-}${FM1_DOCK:-}" ]; then
    echo "CPU rows: skipped (set FM1_REF or the FM-1 build inputs; see tools/gate_cpu.sh)"
else
    mkdir -p "$G/cpu"
    echo "CPU rows, fm1-emu strict, worst % of a block (device ~= x 1.08):"
    echo "base:"; cpu "$B" || echo "CPU rows: base failed"
    echo "head:"; cpu "$H" || { echo "CPU rows: head failed"; cpu_failed=1; }   # (a mean over the limit too)
fi
[ "$d" -eq 0 ] && [ "${cpu_failed:-0}" = 0 ] || exit 1
