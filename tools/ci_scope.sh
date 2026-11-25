#!/bin/sh
# ci_scope.sh [BASE]: which of CI's jobs a change needs.
#   page  every changed path belongs to the project page (#356): site/ and its Pages workflow, with or without
#         docs. CI skips its full build and tests and runs the page's own tests.
#   docs  every changed path is Markdown no check reads: README.md, and docs/**/*.md but docs/patches.md
#         (tools/jx8p.py makes it, check-patches reads it) and docs/ui/interaction.md (tests/test_ui.c reads
#         it). CI skips its build and tests; no test reads these.
#   full  anything else, or no paths at all. tools/site_build.py is not a page file: make check tests it.
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
