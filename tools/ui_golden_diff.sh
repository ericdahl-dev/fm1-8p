#!/bin/sh
# ui_golden_diff.sh - every value whose screen text differs between BASE and this tree (#196).
# tests/ui_format_golden.h pins the text by a hash and a few readable rows; this lists every value
# behind a moved hash. It builds this tree's display walk (tests/ui_golden_main.c) against BASE's
# firmware, so BASE needs no walk of its own.
#
# usage: tools/ui_golden_diff.sh BASE      (make ui-golden BASE=<commit>)
#   BASE   a commit whose firmware has ui_value, ui_format and ui_column and whose Makefile names
#          ENGINE_OBJ, HOST_OBJ, BENCH_OBJ and FW_OBJ; an older tree does not build, and this exits 2.
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
if [ ! -f "$d/.complete" ]; then             # (archived into a fresh folder, checked, then put in place)
    rm -rf "$d" "$d.tmp" && mkdir -p "$d.tmp" || exit 2
    git -C "$ROOT" archive -o "$d.tar" "$c" || die "$1: cannot archive"
    tar -x -C "$d.tmp" -f "$d.tar" && rm -f "$d.tar" || die "$1: cannot unpack"
    [ -f "$d.tmp/Makefile" ] || die "$1: its tree has no Makefile"
    mv "$d.tmp" "$d" && : >"$d/.complete" || exit 2
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
# the values that differ, by param in the order they come: the first 8 of each, then how many in all
awk -F '\t' '
    NR == FNR { was[$1] = $2; base[++nb] = $1; next }
    { now[$1] = $2; if (!($1 in was)) { add($1, "(none)", $2) } else if (was[$1] != $2) { add($1, was[$1], $2) } }
    END {
        for (j = 1; j <= nb; j++) if (!(base[j] in now)) add(base[j], was[base[j]], "(none)")
        for (j = 1; j <= np; j++) {
            p = order[j]
            printf "%s: %d value%s differ%s\n", p, count[p], count[p] == 1 ? "" : "s", count[p] == 1 ? "s" : ""
            for (i = 1; i <= count[p] && i <= 8; i++) print "    " line[p, i]
            if (count[p] > 8) printf "    ... and %d more\n", count[p] - 8
        }
    }
    function add(key, a, b,    p) {
        p = key; sub(/ .*/, "", p)
        if (!(p in count)) order[++np] = p
        count[p]++
        if (count[p] <= 8) line[p, count[p]] = key ": \"" a "\" -> \"" b "\""
    }' "$G/base.txt" "$G/head.txt"
exit 1
