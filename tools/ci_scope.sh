#!/bin/sh
# ci_scope.sh [BASE]: which of CI's jobs a change needs.
#   page  every changed path belongs to the project page (#356): site/ and its Pages workflow, with or without
#         docs. CI skips its full build and tests and runs the page's own tests.
#   docs  every changed path is Markdown no check reads: README.md, and docs/**/*.md but docs/patches.md
#         (tools/jx8p.py makes it, check-patches reads it) and docs/ui/interaction.md (tests/test_ui.c reads
#         it). CI skips its build and tests; no test reads these.
#   full  anything else, or no paths at all. tools/site_build.py is not a page file: make check tests it.
#
#
# ci_scope.sh --scorecard [BASE]: "scorecard" when the change touches the scorecard tests' inputs (their code,
# the MIDI and the bank they render, the CI workflow, or pins.env's numpy and scipy lines; #407), else "skip".
# Those tests take about 10 minutes; a push to main runs them always (the workflow). From a bare list,
# pins.env counts whole (its lines unseen); no paths, or a base that isn't there: "scorecard".
#
# With BASE, the paths are the changes from BASE to HEAD in the current repository, listed with rename
# detection off, so a file moved into site/ also lists its old path; a BASE that is empty or not there, or
# a failing diff, prints "full". Without BASE, the paths are read from stdin, one per line.
classify() {
    n=0 page=0
    while IFS= read -r p || [ -n "$p" ]; do
        [ -n "$p" ] || continue
        n=$((n + 1))
        case $p in
            site/*|.github/workflows/pages.yml) page=1 ;;
            docs/patches.md|docs/ui/interaction.md) echo full; return ;;
            README.md|docs/*.md) ;;
            *) echo full; return ;;
        esac
    done
    if [ "$n" -eq 0 ]; then echo full; elif [ "$page" -eq 1 ]; then echo page; else echo docs; fi
}
scorecard_inputs() {   # stdin: paths; $1: the pins.env diff (or "whole")
    n=0
    while IFS= read -r p || [ -n "$p" ]; do
        [ -n "$p" ] || continue
        n=$((n + 1))
        case $p in
            tools/scorecard*.py|tools/scorecard.sh|tools/gate_midi.py|tools/jx8p.py|tools/params.py|\
            data/jx8p-factory-1-32.syx|tests/test_scorecard*|.github/workflows/ci.yml) echo scorecard; return ;;
            firmware/pins.env)
                if [ "$1" = whole ] || printf '%s\n' "$1" | grep -Eq '^[-+](NUMPY|SCIPY)_VERSION='; then
                    echo scorecard; return
                fi ;;
        esac
    done
    if [ "$n" -eq 0 ]; then echo scorecard; else echo skip; fi
}
if [ "${1:-}" = --scorecard ]; then
    if [ $# -lt 2 ]; then
        scorecard_inputs whole
        exit 0
    fi
    base=$2
    if [ -z "$base" ] || ! git cat-file -e "$base^{commit}" 2>/dev/null; then
        echo scorecard
        exit 0
    fi
    paths=$(git diff --no-renames --name-only "$base" HEAD) || { echo scorecard; exit 0; }
    pins=$(git diff "$base" HEAD -- firmware/pins.env) || pins=whole
    printf '%s\n' "$paths" | scorecard_inputs "$pins"
    exit 0
fi
if [ $# -eq 0 ]; then
    classify
    exit 0
fi
base=$1
if [ -z "$base" ] || ! git cat-file -e "$base^{commit}" 2>/dev/null; then
    echo full
    exit 0
fi
paths=$(git diff --no-renames --name-only "$base" HEAD) || { echo full; exit 0; }
printf '%s\n' "$paths" | classify
