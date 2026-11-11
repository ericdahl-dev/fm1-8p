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
# Only tinytest's summary, the last line, counts: a test may print the same words itself.
printf '#!/bin/sh\necho "2 tests, 0 failed"\necho "ok     a test that prints a count"\n' >"$T/early"; chmod +x "$T/early"
expect_fail "a count before the last line does not count" sh tools/run-tests.sh "$T/early"

printf '#!/bin/sh\necho "ok   one"\nprintf "\\n3 tests, 0 failed\\n"\n' >"$T/pass"; chmod +x "$T/pass"
expect_ok   "a binary that ran its tests and passed passes" sh tools/run-tests.sh "$T/pass"
expect_ok   "its output is shown" grep -q "ok   one" "$T/out.last"

# Sanitizers and failed checks may write to stderr: it must reach the log too.
printf '#!/bin/sh\necho "warning from stderr" >&2\nprintf "\\n3 tests, 0 failed\\n"\n' >"$T/stderr"; chmod +x "$T/stderr"
sh tools/run-tests.sh "$T/stderr" 2>/dev/null >"$T/stderr.out"
expect_ok   "stderr is shown on stdout with the rest" grep -q "warning from stderr" "$T/stderr.out"

# A sanitizer abort mid-run must not lose the ok lines before it, though stdout is a pipe or file.
printf '#include <stdlib.h>\n#include "tinytest.h"\nTT_TEST(passes) { TT_CHECK(1); }\nTT_TEST(aborts) { abort(); }\nint main(void) { TT_RUN(passes); TT_RUN(aborts); return tt_report(); }\n' >"$T/abort.c"
${CC:-cc} -Itests -o "$T/abort" "$T/abort.c"
expect_fail "an abort mid-run fails" sh tools/run-tests.sh "$T/abort"
expect_ok   "and keeps the ok lines before it" grep -q "ok     passes" "$T/out.last"

# Output is live: a line shows while the binary still runs (it waits up to 5 s for the go file).
cat >"$T/slow" <<EOS
#!/bin/sh
echo started
i=0; while [ ! -e "$T/go" ] && [ \$i -lt 50 ]; do sleep 0.1; i=\$((i + 1)); done
printf "\n1 tests, 0 failed\n"
EOS
chmod +x "$T/slow"
sh tools/run-tests.sh "$T/slow" >"$T/slow.out" 2>&1 &
slow_pid=$!
i=0; while ! grep -q started "$T/slow.out" && [ $i -lt 30 ]; do sleep 0.1; i=$((i + 1)); done
expect_ok   "output shows while the binary runs" grep -q started "$T/slow.out"
: >"$T/go"
expect_ok   "and the run still passes" wait "$slow_pid"

# LeakSanitizer reports after the test count and makes the exit status nonzero.
printf '#!/bin/sh\nprintf "\\n3 tests, 0 failed\\n"\nexit 23\n' >"$T/leak"; chmod +x "$T/leak"
expect_fail "a nonzero exit fails even after a clean count" sh tools/run-tests.sh "$T/leak"
expect_fail "a missing test binary fails" sh tools/run-tests.sh "$T/gone"
expect_ok   "and says it is missing" grep -q "gone does not exist" "$T/out.last"
expect_fail "no temporary directory fails" env TMPDIR="$T/nodir" sh tools/run-tests.sh "$T/pass"
expect_ok   "and says so" grep -q "cannot make a temporary directory" "$T/out.last"
# macOS's mktemp -d, given no template, ignores TMPDIR (it uses the per-user temp folder), so
# the check above passed there without the runner looking at TMPDIR. A mktemp that does the same:
mkdir "$T/bsd"
printf '#!/bin/sh\nif [ "$*" = "-d" ]; then TMPDIR=%s exec %s -d; fi\nexec %s "$@"\n' \
    "$T" "$(command -v mktemp)" "$(command -v mktemp)" >"$T/bsd/mktemp"; chmod +x "$T/bsd/mktemp"
expect_fail "no temporary directory fails where mktemp ignores TMPDIR" \
    env PATH="$T/bsd:$PATH" TMPDIR="$T/nodir" sh tools/run-tests.sh "$T/pass"
expect_ok   "and says so" grep -q "cannot make a temporary directory" "$T/out.last"
expect_fail "no argument fails" sh tools/run-tests.sh
expect_ok   "and prints the usage" grep -q "usage: tools/run-tests.sh BIN" "$T/out.last"

echo
echo "$run tests, $failed failed"
[ "$failed" -eq 0 ]
