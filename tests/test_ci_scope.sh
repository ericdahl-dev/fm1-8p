#!/bin/sh
# tools/ci_scope.sh: CI's full build runs unless every changed path is the project page's (#356).
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd -P)
fail=0
nl='
'
report() {   # report GOT WANT DESCRIPTION
    if [ "$1" = "$2" ]; then echo "ok     $3"; else echo "FAIL   $3: got '$1', want '$2'"; fail=1; fi
}
check() {   # check WANT PATHS DESCRIPTION: PATHS one per line on stdin, as git diff --name-only lists them
    report "$(printf '%s' "$2" | sh "$ROOT/tools/ci_scope.sh")" "$1" "$3"
}
check page "site/index.html${nl}site/style.css" "page files only: the page"
check page "site/index.html${nl}site/style.css${nl}" "the same, ending in a newline as git's list does"
check page ".github/workflows/pages.yml" "the Pages workflow only: the page"
check full "site/index.html${nl}engine/voice.c" "the page and engine code: full"
check full "tools/site_build.py" "the page's generator (tested by make check): full"
check full "README.md" "anything else: full"
check full "site.html" "a name that only starts like site/: full"
check full '"site/caf\303\251.png"' "a quoted name (git quotes non-ASCII): full"
check full "" "no list (a new branch, an unknown base): full"

# With a BASE, the script lists the changes from BASE to HEAD itself, in a scratch repository.
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
git -C "$T" init -q && git -C "$T" config user.email t@t && git -C "$T" config user.name t
mkdir -p "$T/site" "$T/engine"
echo page >"$T/site/index.html"; echo code >"$T/engine/a.c"
git -C "$T" add -A && git -C "$T" commit -qm base
base=$(git -C "$T" rev-parse HEAD)
scope() { (cd "$T" && sh "$ROOT/tools/ci_scope.sh" "$1"); }
echo changed >"$T/site/index.html"; git -C "$T" commit -qam page
report "$(scope "$base")" page "BASE: a page edit is the page"
git -C "$T" mv engine/a.c site/a.c; git -C "$T" commit -qm move
report "$(scope "$base")" full "BASE: code moved into site/ is a code change (no rename detection)"
report "$(scope 0000000000000000000000000000000000000000)" full "BASE: a base that isn't there runs everything"
report "$(scope "")" full "BASE: an empty base runs everything"
exit $fail
