#!/bin/sh
# ci_scope.sh [BASE]: prints "page" when every changed path belongs to the project page (#356): site/ and its
# Pages workflow. CI then skips its full build and tests (it still runs the page's own tests). Anything else,
# or no paths at all, prints "full". tools/site_build.py is not a page file: make check tests it.
#
# With BASE, the paths are the changes from BASE to HEAD in the current repository, listed with rename
# detection off, so a file moved into site/ also lists its old path; a BASE that is empty or not there, or
# a failing diff, prints "full". Without BASE, the paths are read from stdin, one per line.
classify() {
    n=0
    while IFS= read -r p || [ -n "$p" ]; do
        [ -n "$p" ] || continue
        n=$((n + 1))
        case $p in
            site/*|.github/workflows/pages.yml) ;;
            *) echo full; return ;;
        esac
    done
    [ "$n" -gt 0 ] && echo page || echo full
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
