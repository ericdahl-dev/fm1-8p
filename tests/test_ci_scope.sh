#!/bin/sh
# tools/ci_scope.sh: CI's full build runs unless every changed path is the project page's (#356).
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd -P)
fail=0
nl='
'
check() {   # check WANT PATHS DESCRIPTION: PATHS one per line, as git diff --name-only lists them
    got=$(printf '%s' "$2" | sh "$ROOT/tools/ci_scope.sh")
    if [ "$got" = "$1" ]; then echo "ok     $3"; else echo "FAIL   $3: got '$got', want '$1'"; fail=1; fi
}
check page "site/index.html${nl}site/style.css" "page files only: the page"
check page ".github/workflows/pages.yml" "the Pages workflow only: the page"
check full "site/index.html${nl}engine/voice.c" "the page and engine code: full"
check full "tools/site_build.py" "the page's generator (tested by make check): full"
check full "README.md" "anything else: full"
check full "site.html" "a name that only starts like site/: full"
check full "" "no list (a new branch, an unknown base): full"
exit $fail
