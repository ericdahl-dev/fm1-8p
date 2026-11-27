#!/bin/sh
# gate.sh - the no-sound gate (#140): renders every clip of tools/gate_cases.py on two source trees and
# compares them byte for byte. A change that should not change the sound must pass it.
#
# usage: tools/gate.sh BASE [HEAD]      (make gate BASE=<commit> [HEAD=<commit>])
#   BASE, HEAD   a git commit (or branch) of this repo, or a directory holding a source tree;
#                HEAD defaults to the commit checked out.
#   GATE_DIR     where trees, builds and renders go (default build/gate); a tree's renders are kept,
#                keyed by a hash of its sources, the clip list, the run's MIDI and the host compiler
#                (tools/gate_run.py), so the next run against the same BASE renders only HEAD.
# The run's records (tools/gate_record.py): GATE_DIR/records.jsonl, a clip each and HEAD's CPU run.
# Exit status (tools/gate_record.py verdict): 0 every clip identical (and HEAD's CPU rows, when they run,
# pass); 1 some clip differs, or a CPU row of HEAD has its mean over GATE_CPU_LIMIT (only a layout over it
# warns); 2 the gate could not run, HEAD's CPU rows among it (a layout that could not run or has no
# figures): an error over a fail.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd -P)
G=${GATE_DIR:-$ROOT/build/gate}
PY=${PYTHON:-python3}   # a command and its flags: used unquoted
mkdir -p "$G" || exit 2
G=$(cd "$G" && pwd -P)

die() { echo "gate: $*" >&2; exit 2; }
REC=$G/records.jsonl                    # this run's records: none of an earlier run's stand for it
rm -f "$REC" || die "cannot clear $REC"

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

# rkey DIR: sets k to DIR's render key (tools/gate_run.py: its sources, the case list, every MIDI file of the
# run, the host compiler), or stops the gate: two empty keys would let HEAD reuse BASE's renders
rkey() { k=$($PY -B "$ROOT/tools/gate_run.py" render-key "$1" "$ROOT" "$G/midi") && [ ${#k} -eq 16 ] || die "$1: cannot hash its sources"; }

# renders DIR: renders every clip with DIR's renderer into $G/out/<key> (once per key); prints that folder.
# Only the clips DIR's own tools/gate_cases.py lists, if it has one: an older tree can't render a clip
# added since (its params, its MIDI), and the comparison reports such a clip as new.
renders() {
    rkey "$1"; o=$G/out/$k
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
rm -rf "$G/midi" && mkdir -p "$G/midi" && cp "$ROOT/examples/demo.mid" "$G/midi/demo.mid" &&
    $PY "$ROOT/tools/gate_midi.py" "$G/midi" || die "cannot write the MIDI"
B=$(src "$1") || exit 2
H=$(src "${2:-HEAD}") || exit 2
OB=$(renders "$B") || exit 2
OH=$(renders "$H") || exit 2

# coverage DIR: renders every clip on a --coverage build of DIR's renderer, one .gcda set per clip,
# and prints how many clips reach each engine function (tools/gate_coverage.py)
coverage() {
    rkey "$1"; c=$G/cov/$k
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

# cpu DIR: the CPU rows of DIR (tools/gate_cpu.sh, or GATE_CPU: a stand-in for tests), kept by their key
# (tools/gate_run.py cpu-key: DIR's sources, the script, its pad, the Python that stages, reads and judges
# the rows, its toggles GATE_CPU_LAYOUTS, GATE_CPU_HINOTE and GATE_CPU_LIMIT, and the FM-1 tools' identity
# from gate_cpu.sh --tools): a row added there or skipped by a toggle must not come back missing, nor a
# rule or a tool changed there leave the old rows standing. A failed run's rows are shown, not kept; its
# return is gate_cpu.sh's exit. A run that exits 0 with no rows ran nothing: kept nowhere, it returns 2
# (#179).
cpu() {
    s=${GATE_CPU:-$ROOT/tools/gate_cpu.sh}
    t=$G/cpu/.tools.json keep=1
    # tools that can't be identified: the rows are made and shown, never kept (a later tool could not be
    # told apart); gate_cpu.sh below stops with its own line when its inputs are missing
    sh "$s" --tools >"$t" 2>/dev/null || { printf '{"fm1": "not to be had"}\n' >"$t"; keep=0; }
    k=$($PY -B "$ROOT/tools/gate_run.py" cpu-key "$1" "$ROOT" "$s" "$t") && [ ${#k} -eq 16 ] || die "$1: cannot hash its CPU inputs"
    o=$G/cpu/$k
    # (a kept rows.txt with no rows, from a gate before #179, is run again: it ran nothing)
    if [ "$keep" = 0 ] || ! grep -q '[^[:space:]]' "$o/rows.txt" 2>/dev/null; then
        sh "$s" "$1" "$o/runs" >"$o.rows" 2>"$o.err"
        r=$?
        if [ "$r" -ne 0 ]; then sort "$o.rows"; cat "$o.err" >&2; rm -f "$o.rows" "$o.err"; return "$r"; fi
        if ! grep -q '[^[:space:]]' "$o.rows"; then
            cat "$o.err" >&2; rm -f "$o.rows" "$o.err"
            echo "gate: $1: the CPU run exited 0 with no rows" >&2; return 2
        fi
        if [ "$keep" = 0 ]; then sort "$o.rows"; rm -f "$o.rows" "$o.err"; return 0; fi
        mkdir -p "$o" && sort "$o.rows" >"$o/rows.txt" && rm -f "$o.rows" "$o.err"
    fi
    cat "$o/rows.txt"
}

# the clips compared byte for byte (tools/gate_run.py clips): their records into $REC, the
# lines on stdout; a clip that can't be compared, or none compared, stops the gate
$PY -B "$ROOT/tools/gate_run.py" clips "$OB" "$OH" "$REC" || exit 2

if [ -z "${FM1_REF:-}${FM1_DOCK:-}" ]; then
    echo "CPU rows: skipped (set FM1_REF or the FM-1 build inputs; see tools/gate_cpu.sh)"
else
    mkdir -p "$G/cpu"
    echo "CPU rows, fm1-emu strict, worst % of a block (device ~= x 1.08):"
    echo "base:"; cpu "$B" || echo "CPU rows: base failed"
    echo "head:"; cpu "$H"; r=$?
    [ "$r" -eq 0 ] || echo "CPU rows: head failed"             # (a mean over the limit too)
    $PY -B "$ROOT/tools/gate_run.py" cpu-record "$REC" "$r" || exit 2
    cpu_ran=cpu
fi
# one rule decides (tools/gate_record.py verdict): 2 the gate could not run (HEAD's CPU rows could not
# run among it), 1 a clip differs or HEAD's CPU mean is over the limit, else 0
$PY -B "$ROOT/tools/gate_run.py" judge "$REC" "$OH" ${cpu_ran:-}
