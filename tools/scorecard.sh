#!/bin/sh
# scorecard.sh - the factory-tone scorecard (#97): how close COMMIT's 32 JX-8P factory tones are to
# PG-8X 2.0.13, and (with the match harness) every tone parameter. Appends to a JSONL history.
#
# usage: tools/scorecard.sh COMMIT [BASE]        (make scorecard COMMIT=<commit> [BASE=<commit>])
#   SCORECARD_REF       the PG-8X reference renders: NN.wav (and NN-1.wav .. for a reference of several draws,
#                       manifest shifts_ticks, s12) and manifest.json (made outside this repo;
#                       no PG-8X audio is committed). Default: $FM1_REF/pg8x/scorecard/v5 (PG-8X 2.0.13,
#                       SELFNOISE 0: #247; OCTAVE STRETCH at PG-8X's default 0.4, which plays a saw 1.4
#                       cents flat at C4 and 2.1 at C6, where 0.5 is exact. Ours stays exact: Eric, #260).
#   SCORECARD_HISTORY   history.jsonl to append to (default build/scorecard/history.jsonl)
#   SCORECARD_PYTHON    a Python with numpy and scipy (default python3)
#   SCORECARD_CACHE     a folder for each tone's score, kept by the hash of its renders, the reference and the
#                       scorecard's code, so a commit whose sound didn't change scores in the time of its
#                       renders (default build/scorecard/cache; SCORECARD_CACHE= turns it off)
#   SCORECARD_MATCH     the match harness's match.py, and SCORECARD_MATCH_PYTHON and SCORECARD_MATCH_CACHE
#                       to run it with: adds a row per tone parameter. Skipped when unset (CI).
#   SCORECARD_BANK      another bank of 32 tones (.syx) to score instead of the factory bank, with
#                       SCORECARD_BANK_NAME for the history rows; its reference must be rendered from it.
#                       Keep a bank that isn't ours out of this repo. The parameter sweep is skipped then.
#   BASE                a commit already in the history: prints the tones that moved against it.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd -P)
die() { echo "scorecard: $*" >&2; exit 2; }
[ $# -ge 1 ] || die "usage: scorecard.sh COMMIT [BASE]"
[ -n "${SCORECARD_REF:-}" ] || { [ -n "${FM1_REF:-}" ] && SCORECARD_REF=$FM1_REF/pg8x/scorecard/v5; } ||
    die "set SCORECARD_REF to the PG-8X reference folder (or FM1_REF: its pg8x/scorecard/v5)"
[ -f "$SCORECARD_REF/manifest.json" ] || die "$SCORECARD_REF: no manifest.json (the PG-8X reference: v5)"
c=$(git -C "$ROOT" rev-parse --verify --quiet "$1^{commit}") || die "$1: not a commit"
s=$(git -C "$ROOT" rev-parse --short "$c")
b=   # BASE's short hash, resolved before any work: a BASE that is not a commit stops here (#335), not as --base ""
if [ $# -ge 2 ]; then
    b=$(git -C "$ROOT" rev-parse --verify --quiet "$2^{commit}") || die "$2: not a commit"
    b=$(git -C "$ROOT" rev-parse --short "$b") && [ -n "$b" ] || die "$2: cannot shorten its hash"
fi
W=$ROOT/build/scorecard/$s
if [ ! -f "$W/src/.complete" ]; then
    rm -rf "$W/src" && mkdir -p "$W/src" && git -C "$ROOT" archive "$c" | tar -x -C "$W/src" || die "cannot archive $1"
    : >"$W/src/.complete"
fi
make -s -C "$W/src" build/fm1-8p-render >"$W/build.log" 2>&1 || { cat "$W/build.log" >&2; die "$1 does not build"; }
match=
if [ -n "${SCORECARD_MATCH:-}" ] && [ -z "${SCORECARD_BANK:-}" ]; then
    rm -rf "$W/match"
    ${SCORECARD_MATCH_PYTHON:-python3} -I "$SCORECARD_MATCH" --repo "$W/src" --out "$W/match" --all \
        ${SCORECARD_MATCH_CACHE:+--cache "$SCORECARD_MATCH_CACHE"} >"$W/match.log" 2>&1 || die "the match harness failed (see $W/match.log)"
    match="--match $W/match"
fi
cache=${SCORECARD_CACHE-$ROOT/build/scorecard/cache}   # (set and empty: no cache)
# shellcheck disable=SC2086 # $match is two words or none
${SCORECARD_PYTHON:-python3} -B "$ROOT/tools/scorecard.py" --tree "$W/src" --commit "$s" --ref "$SCORECARD_REF" \
    --history "${SCORECARD_HISTORY:-$ROOT/build/scorecard/history.jsonl}" --work "$W/work${SCORECARD_BANK_NAME:+-$SCORECARD_BANK_NAME}" $match \
    ${SCORECARD_BANK:+--bank "$SCORECARD_BANK" --bank-name "${SCORECARD_BANK_NAME:?set SCORECARD_BANK_NAME}"} \
    ${b:+--base "$b" --root "$ROOT"} ${cache:+--cache "$cache"}
