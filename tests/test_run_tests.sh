#!/bin/sh
# Tests for tools/run-tests.sh, which make test runs the C test binary through.
# usage: sh tests/test_run_tests.sh       (run from the repo root)
set -u
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
run=0 failed=0
ok()   { run=$((run + 1)); echo "ok     $1"; }
fail() { run=$((run + 1)); failed=$((failed + 1)); echo "not ok $1"; }
# Each check's output goes to $T/out; the one before it is kept in $T/out.last.
keep()        { [ -f "$T/out" ] && mv "$T/out" "$T/out.last"; return 0; }
expect_ok()   { t_name=$1; shift; keep; if "$@" >"$T/out" 2>&1; then ok "$t_name"; else fail "$t_name"; cat "$T/out"; fi; }
expect_fail() { t_name=$1; shift; keep; if "$@" >"$T/out" 2>&1; then fail "$t_name"; else ok "$t_name"; fi; }

# A link cut short (a killed build) can leave a 0-byte binary; a shell runs it as an empty script
# and exits 0, so the tests would look green without running.
: >"$T/empty"; chmod +x "$T/empty"
expect_fail "an empty test binary fails" sh tools/run-tests.sh "$T/empty"
expect_ok   "and says it is empty" grep -q "is empty" "$T/out.last"

# Exit 0 is not enough: the binary must say it ran tests, and how many.
printf '#!/bin/sh\nexit 0\n' >"$T/silent"; chmod +x "$T/silent"
expect_fail "a binary that exits 0 without a test count fails" sh tools/run-tests.sh "$T/silent"
printf '#!/bin/sh\nprintf "\\n0 tests, 0 failed\\n"\n' >"$T/none"; chmod +x "$T/none"
expect_fail "a binary that ran 0 tests fails" sh tools/run-tests.sh "$T/none"

printf '#!/bin/sh\necho "ok   one"\nprintf "\\n3 tests, 0 failed\\n"\n' >"$T/pass"; chmod +x "$T/pass"
expect_ok   "a binary that ran its tests and passed passes" sh tools/run-tests.sh "$T/pass"
expect_ok   "its output is shown" grep -q "ok   one" "$T/out.last"

# LeakSanitizer reports after the test count and makes the exit status nonzero.
printf '#!/bin/sh\nprintf "\\n3 tests, 0 failed\\n"\nexit 23\n' >"$T/leak"; chmod +x "$T/leak"
expect_fail "a nonzero exit fails even after a clean count" sh tools/run-tests.sh "$T/leak"
expect_fail "a missing test binary fails" sh tools/run-tests.sh "$T/missing"

echo
echo "$run tests, $failed failed"
[ "$failed" -eq 0 ]
