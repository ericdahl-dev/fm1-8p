#!/bin/sh
# make check-site-js without node: under CI it fails (the installer's tests must not be skipped there, #362 review
# L2); elsewhere it skips with a message (node is optional for make check on a desktop).
set -u
MAKE=$(command -v make)
fail=0
out=$(env -i PATH=/nonexistent CI=true "$MAKE" -s check-site-js 2>&1); r=$?
if [ "$r" -eq 0 ]; then echo "test_check_site_js: under CI without node it passed: $out" >&2; fail=1; fi
out=$(env -i PATH=/nonexistent "$MAKE" -s check-site-js 2>&1); r=$?
if [ "$r" -ne 0 ]; then echo "test_check_site_js: without CI and node it failed (exit $r): $out" >&2; fail=1; fi
case $out in *skipped*) ;; *) echo "test_check_site_js: no skip message: $out" >&2; fail=1 ;; esac
[ "$fail" -eq 0 ] && echo "test_check_site_js: ok"
exit "$fail"
