#!/bin/sh
# ci_scope.sh: reads the changed paths (one per line, as `git diff --name-only` lists them) and prints
# "page" when every one belongs to the project page (#356): site/ and its Pages workflow. CI then skips its
# full build and tests. Anything else, or no list at all, prints "full". tools/site_build.py is not a page
# file: make check tests it.
n=0
while IFS= read -r p || [ -n "$p" ]; do
    [ -n "$p" ] || continue
    n=$((n + 1))
    case $p in
        site/*|.github/workflows/pages.yml) ;;
        *) echo full; exit 0 ;;
    esac
done
[ "$n" -gt 0 ] && echo page || echo full
