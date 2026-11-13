#!/bin/sh
# gate.sh - the no-sound gate (#140): renders every clip of tools/gate_cases.py on two source trees and
# compares them byte for byte. A change that should not change the sound must pass it.
#
# usage: tools/gate.sh BASE [HEAD]      (make gate BASE=<commit> [HEAD=<commit>])
#   BASE, HEAD   a git commit (or branch) of this repo, or a directory holding a source tree;
#                HEAD defaults to the commit checked out.
#   GATE_DIR     where trees, builds and renders go (default build/gate); a tree's renders are kept,
#                keyed by a hash of its sources, so the next run against the same BASE renders only HEAD.
# Exit status: 0 every clip identical, 1 some clip differs, 2 the gate could not run.
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

# key DIR: a hash of the tree's sources that a render depends on
key() {
    (cd "$1" && find Makefile engine host firmware tools data examples -type f ! -path '*/__pycache__/*' |
        LC_ALL=C sort | xargs sha256sum) | sha256sum | cut -c1-16
}

# renders DIR: renders every clip with DIR's renderer into $G/out/<key> (once per key); prints that folder
renders() {
    k=$(key "$1"); o=$G/out/$k
    if [ ! -f "$o/.complete" ]; then
        rm -rf "$o" && mkdir -p "$o" "$G/build/$k"
        make -s -C "$1" BUILD="$G/build/$k" "$G/build/$k/fm1-8p-render" >"$o/.build.log" 2>&1 ||
            { cat "$o/.build.log" >&2; die "$1: the renderer does not build"; }
        $PY -B "$ROOT/tools/gate_cases.py" "$1" >"$o/.cases" || die "$1: cannot list the clips"
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
    k=$(key "$1"); c=$G/cov/$k
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

# cpu DIR: the CPU rows of DIR (tools/gate_cpu.sh), kept by DIR's key like its renders
cpu() {
    k=$(key "$1"); o=$G/cpu/$k
    if [ ! -f "$o/rows.txt" ]; then
        sh "$ROOT/tools/gate_cpu.sh" "$1" "$o/runs" >"$o.rows" 2>"$o.err" || { cat "$o.err" >&2; return 1; }
        mkdir -p "$o" && sort "$o.rows" >"$o/rows.txt" && rm -f "$o.rows" "$o.err"
    fi
    cat "$o/rows.txt"
}

n=0 d=0
while IFS='	' read -r name mid args; do
    n=$((n + 1))
    if ! cmp -s "$OB/$name.wav" "$OH/$name.wav"; then d=$((d + 1)); echo "differs: $name"; fi
done <"$OH/.cases"
echo "$d of $n clips differ"

if [ -z "${FM1_REF:-}${FM1_DOCK:-}" ]; then
    echo "CPU rows: skipped (set FM1_REF or the FM-1 build inputs; see tools/gate_cpu.sh)"
else
    mkdir -p "$G/cpu"
    echo "CPU rows, fm1-emu strict, worst % of a block (device ~= x 1.08):"
    echo "base:"; cpu "$B" || echo "CPU rows: base failed"
    echo "head:"; cpu "$H" || echo "CPU rows: head failed"
fi
[ "$d" -eq 0 ] || exit 1
