#!/bin/sh
# ui_golden_diff.sh - every value whose screen text differs between BASE and this tree (#196).
# tests/ui_format_golden.h pins the text by a hash and a few readable rows; this lists every value
# behind a moved hash. It builds this tree's display walk (tests/ui_golden_main.c) against BASE's
# firmware, so BASE needs no walk of its own.
#
# usage: tools/ui_golden_diff.sh BASE      (make ui-golden BASE=<commit>)
#   UI_GOLDEN_DIR   where BASE's tree and builds go (default build/ui-golden-base)
# Exit status: 0 the same text everywhere, 1 some value differs, 2 it could not run.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd -P)
G=${UI_GOLDEN_DIR:-$ROOT/build/ui-golden-base}
die() { echo "ui_golden_diff: $*" >&2; exit 2; }
[ $# -eq 1 ] || die "usage: tools/ui_golden_diff.sh BASE"
c=$(git -C "$ROOT" rev-parse --verify --quiet "$1^{commit}") || die "$1: not a commit"
mkdir -p "$G" || exit 2
d=$G/src/$c
if [ ! -f "$d/.complete" ]; then
    rm -rf "$d" && mkdir -p "$d" && git -C "$ROOT" archive "$c" | tar -x -C "$d" || die "$1: cannot archive"
    : >"$d/.complete"
fi
# this tree's walk, built in BASE's tree with BASE's own Makefile
cp "$ROOT/tests/ui_format_walk.h" "$ROOT/tests/ui_golden_main.c" "$ROOT/tests/ui_golden.mk" "$d/tests/" || exit 2
make -s -C "$d" -f Makefile -f tests/ui_golden.mk build/ui-golden >"$G/base-build.log" 2>&1 ||
    die "BASE's walk did not build (see $G/base-build.log)"
make -s -C "$ROOT" build/ui-golden >"$G/head-build.log" 2>&1 || die "this tree's walk did not build"
"$d/build/ui-golden" --dump >"$G/base.txt" || die "BASE's walk failed"
"$ROOT/build/ui-golden" --dump >"$G/head.txt" || die "this tree's walk failed"
if cmp -s "$G/base.txt" "$G/head.txt"; then
    echo "ui_golden_diff: every value reads as at ${c%"${c#????????}"}"
    exit 0
fi
# the values that differ, by param: the first 8 of each, then how many in all
awk -F '\t' '
    NR == FNR { was[$1] = $2; next }
    { now[$1] = $2; if (!($1 in was)) { add($1, "(none)", $2) } else if (was[$1] != $2) { add($1, was[$1], $2) } }
    END {
        for (k in was) if (!(k in now)) add(k, was[k], "(none)")
        for (p in count) {
            printf "%s: %d value%s differ\n", p, count[p], count[p] == 1 ? "" : "s"
            for (i = 1; i <= count[p] && i <= 8; i++) print "    " line[p, i]
            if (count[p] > 8) printf "    ... and %d more\n", count[p] - 8
        }
    }
    function add(key, a, b,    p) {
        p = key; sub(/ .*/, "", p)
        count[p]++
        if (count[p] <= 8) line[p, count[p]] = key ": \"" a "\" -> \"" b "\""
    }' "$G/base.txt" "$G/head.txt" | sort -s -t: -k1,1
exit 1
