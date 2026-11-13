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
PY=${PYTHON:-python3}
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
        "$PY" "$ROOT/tools/gate_cases.py" >"$o/.cases" || die "cannot list the clips"
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
    "$PY" "$ROOT/tools/gate_midi.py" "$G/midi" || die "cannot write the MIDI"
B=$(src "$1") || exit 2
H=$(src "${2:-HEAD}") || exit 2
OB=$(renders "$B") || exit 2
OH=$(renders "$H") || exit 2

n=0 d=0
while IFS='	' read -r name mid args; do
    n=$((n + 1))
    if ! cmp -s "$OB/$name.wav" "$OH/$name.wav"; then d=$((d + 1)); echo "differs: $name"; fi
done <"$OH/.cases"
echo "$d of $n clips differ"
[ "$d" -eq 0 ] || exit 1
