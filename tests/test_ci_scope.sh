#!/bin/sh
# tools/ci_scope.sh: CI's full build runs unless every changed path is the project page's (#356) or a
# Markdown doc no check reads.
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
check full "Makefile" "anything else: full"
check docs "README.md" "the README only: docs"
check docs "docs/voices.md${nl}docs/ui/visual.md" "docs Markdown only: docs"
check docs "README.md${nl}docs/modulation.md${nl}" "the README and docs, ending in a newline: docs"
check full "docs/patches.md" "docs/patches.md (tools/jx8p.py makes it, check-patches reads it): full"
check full "docs/ui/interaction.md" "docs/ui/interaction.md (tests/test_ui.c reads it): full"
check full "README.md${nl}docs/patches.md" "the README and a doc a check reads: full"
check full "docs/hub/card.png" "a non-Markdown file under docs/: full"
check full "tests/README.md" "Markdown outside the README and docs/: full"
check full "firmware/notes.md" "Markdown in the firmware: full"
check page "README.md${nl}site/index.html" "docs and the page: the page (its tests run; docs need none)"
check full "README.md${nl}engine/voice.c" "docs and code: full"
check full "site/index.html${nl}docs/patches.md" "the page and a doc a check reads: full"

# The docs scope skips every check, so no check may read a doc it lets through: each tracked Markdown file
# the script classes as docs is named on no line of tests/, tools/, the Makefile or .github/ that reads a
# file (open, fopen, read_text, read_bytes, cat, sed, grep). A new reader of such a doc fails here until
# tools/ci_scope.sh classes that doc as full. CI_SCOPE (tests only) checks another copy of the script.
SCRIPT=${CI_SCOPE:-$ROOT/tools/ci_scope.sh}
for f in $(git -C "$ROOT" ls-files 'README.md' 'docs/*.md'); do
    [ "$(printf '%s\n' "$f" | sh "$SCRIPT")" = docs ] || continue
    b=${f##*/}
    hits=$(git -C "$ROOT" grep -n -F "$b" -- tests tools Makefile .github ':!tests/test_ci_scope.sh' ':!tools/ci_scope.sh' |
           grep -E 'open\(|fopen\(|read_text|read_bytes|cat |sed |grep ' || true)
    report "$hits" "" "no check reads $f (classed docs)"
done
check full "site.html" "a name that only starts like site/: full"
check full '"site/caf\303\251.png"' "a quoted name (git quotes non-ASCII): full"
check full "" "no list (a new branch, an unknown base): full"

# With a BASE, the script lists the changes from BASE to HEAD itself, in a scratch repository.
# The developer's own git config stays out: a global commit.gpgsign or core.hooksPath would
# break or hook these commits.
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
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
echo readme >"$T/README.md"; git -C "$T" add README.md; git -C "$T" commit -qm readme
report "$(scope "$(git -C "$T" rev-parse HEAD~1)")" docs "BASE: a README edit is docs"
git -C "$T" mv engine/a.c site/a.c; git -C "$T" commit -qm move
report "$(scope "$base")" full "BASE: code moved into site/ is a code change (no rename detection)"
report "$(scope 0000000000000000000000000000000000000000)" full "BASE: a base that isn't there runs everything"
report "$(scope "")" full "BASE: an empty base runs everything"

# --scorecard (#407): the scorecard's tests (numpy, about 10 minutes) run only when a change touches their
# inputs; a push to main runs them always (the workflow)
sc() { report "$(printf '%s' "$2" | sh "$ROOT/tools/ci_scope.sh" --scorecard)" "$1" "$3"; }
sc skip "engine/voice.c${nl}firmware/seq.c" "--scorecard: engine and firmware code: skip"
sc skip "README.md" "--scorecard: a doc: skip"
for f in tools/scorecard.py tools/scorecard_cmd.py tools/scorecard.sh tools/gate_midi.py tools/jx8p.py \
         tools/params.py data/jx8p-factory-1-32.syx tests/test_scorecard.py .github/workflows/ci.yml; do
    sc scorecard "engine/voice.c${nl}$f" "--scorecard: $f: scorecard"
done
sc scorecard "firmware/pins.env" "--scorecard: pins.env from a bare list (its lines unseen): scorecard"
sc scorecard "" "--scorecard: no list: scorecard"
scs() { (cd "$T" && sh "$ROOT/tools/ci_scope.sh" --scorecard "$1"); }
mkdir -p "$T/firmware" && printf 'PILLOW_VERSION=1\nNUMPY_VERSION=1\n' >"$T/firmware/pins.env"
git -C "$T" add -A && git -C "$T" commit -qm pins
b2=$(git -C "$T" rev-parse HEAD)
printf 'PILLOW_VERSION=2\nNUMPY_VERSION=1\n' >"$T/firmware/pins.env"; git -C "$T" commit -qam pillow
report "$(scs "$b2")" skip "--scorecard BASE: pins.env, Pillow's line only: skip"
printf 'PILLOW_VERSION=2\nNUMPY_VERSION=2\n' >"$T/firmware/pins.env"; git -C "$T" commit -qam numpy
report "$(scs "$b2")" scorecard "--scorecard BASE: pins.env's numpy line: scorecard"
report "$(scs "")" scorecard "--scorecard BASE: an empty base: scorecard"
exit $fail
