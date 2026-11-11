#!/bin/sh
# A build killed mid-link or mid-compile must not leave an output that make then treats as
# up to date (a 0-byte build/run-tests once passed make test without running a test).
# usage: sh tests/test_build_outputs.sh   (run from the repo root; builds into a temporary dir)
set -u
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
run=0 failed=0
ok()   { run=$((run + 1)); echo "ok     $1"; }
fail() { run=$((run + 1)); failed=$((failed + 1)); echo "not ok $1"; }
REAL_CC=${CC:-cc}

# A compiler that writes an empty output for TARGET and then fails (KILL=fail), or is killed with
# SIGKILL, alone (KILL=cc) or with the make that ran it (KILL=make), as when the session running
# the build dies.
cat >"$T/cc" <<EOS
#!/bin/sh
out=; prev=
for a in "\$@"; do [ "\$prev" = -o ] && out=\$a; prev=\$a; done
case "\$out" in
*"\$TARGET"|*"\$TARGET".tmp)
    : >"\$out"
    if [ "\$KILL" = make ]; then
        p=\$PPID
        while [ -n "\$p" ] && [ "\$p" -gt 1 ]; do
            case "\$(ps -o comm= -p "\$p")" in *make) kill -9 "\$p"; break ;; esac
            p=\$(ps -o ppid= -p "\$p" | tr -d ' ')
        done
    fi
    [ "\$KILL" = fail ] && exit 1
    kill -9 \$\$ ;;
esac
exec $REAL_CC "\$@"
EOS
chmod +x "$T/cc"

B=$T/build
make -s BUILD="$B" CC="$REAL_CC" all "$B/run-tests" >/dev/null 2>&1 || { echo "not ok the build itself"; exit 1; }
for out in run-tests fm1-8p-render fm1-8p-bench engine/env.o; do
    for kill in fail cc make; do
        rm -f "$B/$out"
        (TARGET=$out KILL=$kill make -s BUILD="$B" CC="$T/cc" "$B/$out" >/dev/null 2>&1) 2>/dev/null
        name="$out: no empty output is left when the $kill is killed"
        [ $kill = fail ] && name="$out: no empty output is left when the compiler fails"
        if [ -e "$B/$out" ] && [ ! -s "$B/$out" ]; then fail "$name"; else ok "$name"; fi
    done
    make -s BUILD="$B" CC="$REAL_CC" "$B/$out" >/dev/null 2>&1
    name="$out: the next make builds it"
    if [ -s "$B/$out" ]; then ok "$name"; else fail "$name"; fi
done

echo
echo "$run tests, $failed failed"
[ "$failed" -eq 0 ]
