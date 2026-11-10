#!/bin/sh
# check-engine.sh - prove engine sources use no floating point and no heap.
#
# Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
#
# usage: tools/check-engine.sh OUTDIR source.c...
#
# Four independent layers, any one of which fails the check:
#
#  1. Every source is compiled with the FPU switched off for code
#     generation (-mgeneral-regs-only, plus -mno-sse -mno-80387 on x86),
#     at -O0 (nothing folded away) and -O2. GCC then refuses any
#     floating-point operation outright; Clang turns it into calls to
#     soft-float helpers such as __muldf3, which layer 3 catches.
#  2. The `float` and `double` keywords are #defined to undeclared names,
#     so any floating-point type is a compile error even when unused.
#  3. The external symbols the objects reference (after removing those
#     the engine defines itself) must all be on a short allowlist of
#     memory and integer-arithmetic helpers. That rejects soft-float
#     helpers, libm (sinf, expf, ...) and the heap (malloc, free, ...).
#  4. No 32-bit word of an instruction immediate (on arm64 also the top
#     half a movk sets) or of a data section may fall in 0xFFC00000 to 0xFFCFFFFF: the FM-1 image build (fm1-dock) rejects such words as
#     ROM-looking constants. Desktop code generation differs from the
#     device's, so this catches constants in the source (and most the
#     compiler derives from them), not every word the device build makes.
#
# Environment: CC (default cc).
set -eu

CC=${CC:-cc}
OUT=$1
shift

case "$(uname -m)" in
x86_64 | amd64 | i?86) FPU_OFF="-mgeneral-regs-only -mno-sse -mno-mmx -mno-80387" ;;
aarch64 | arm64) FPU_OFF="-mgeneral-regs-only" ;;
*)
    echo "check-engine: no FPU-off flags known for $(uname -m)" >&2
    exit 2
    ;;
esac
NO_FLOAT_TYPES="-Dfloat=fm1_8p_engine_must_not_use_float"
NO_FLOAT_TYPES="$NO_FLOAT_TYPES -Ddouble=fm1_8p_engine_must_not_use_double"

# Symbols an engine object may reference outside the engine: memory
# helpers the compiler emits for struct copies and clears (Apple clang
# turns memset into bzero or memset_pattern16), stack protection, and
# integer helpers for 64-bit math on 32-bit targets.
ALLOW='^(memset|memcpy|memmove|bzero|memset_pattern(4|8|16)|__stack_chk_fail|__stack_chk_guard|__chkstk_darwin|__(u?(div|mod|divmod)|mul|ashl|ashr|lshr|cmp|ucmp)(di|ti)[34]|__aeabi_(u?ldivmod|u?idiv|u?idivmod|lasr|llsl|llsr|lmul|lcmp|ulcmp|memset|memset4|memset8|memcpy|memcpy4|memcpy8|memclr|memclr4|memclr8|memmove))$'

# Mach-O prefixes C symbols with an underscore.
STRIP='s/^_//'
[ "$(uname -s)" = Darwin ] || STRIP='s/^//'

fail=0
rm -rf "$OUT"
mkdir -p "$OUT"
for opt in -O0 -O2; do
    objs=""
    for src in "$@"; do
        obj="$OUT/$(basename "$src" .c)$opt.o"
        # shellcheck disable=SC2086 # flag lists are meant to split
        if ! $CC -std=c99 $opt $FPU_OFF $NO_FLOAT_TYPES -c "$src" -o "$obj"; then
            echo "check-engine: $src does not build without floating point ($opt)" >&2
            fail=1
            continue
        fi
        objs="$objs $obj"
    done
    [ -n "$objs" ] || continue
    # shellcheck disable=SC2086
    nm -g $objs 2>/dev/null | awk 'NF == 2 && $1 == "U" { print "U", $2 }
                                   NF == 3 && $2 != "U" { print "D", $3 }' \
        >"$OUT/symbols$opt.txt"
    sed -n 's/^D //p' "$OUT/symbols$opt.txt" | sed "$STRIP" | sort -u >"$OUT/defined$opt.txt"
    sed -n 's/^U //p' "$OUT/symbols$opt.txt" | sed "$STRIP" | sort -u >"$OUT/undefined$opt.txt"
    bad=$(comm -23 "$OUT/undefined$opt.txt" "$OUT/defined$opt.txt" | grep -Ev "$ALLOW" || true)
    if [ -n "$bad" ]; then
        echo "check-engine: forbidden external symbols ($opt):" >&2
        echo "$bad" | sed 's/^/    /' >&2
        fail=1
    fi
done

# Layer 4: disassemble (immediates as hex or decimal) and dump data
# sections (little-endian words), then look for 0xFFCxxxxx values.
for obj in "$OUT"/*.o; do
    hits=$(
        {
            objdump -d --no-show-raw-insn "$obj" | sed 's/^/D /'
            objdump -s "$obj" | sed 's/^/S /'
        } 2>/dev/null | awk '
        function rom_hex(h) {   # h: lowercase hex digits, no 0x; either 32-bit word counts
            if (length(h) == 16)
                return substr(h, 1, 3) == "ffc" || substr(h, 9, 3) == "ffc"
            return length(h) == 8 && substr(h, 1, 3) == "ffc"
        }
        $1 == "D" {
            line = tolower($0)
            # arm64 builds constants 16 bits at a time: movk #0xffcX at lsl #16 (w or x
            # register) or lsl #48 (x) puts 0xFFCx at the top of a 32-bit word.
            if (line ~ /movk[ \t]+[wx][0-9]+, #0xffc[0-9a-f], lsl #(16|48)/) { print line; next }
            n = split(line, tok, /[^-0-9a-fx]+/)
            for (i = 1; i <= n; i++) {
                t = tok[i]
                if (t ~ /^-?0x[0-9a-f]+$/) {
                    if (t !~ /^-/ && rom_hex(substr(t, 3))) print line
                } else if (t ~ /^-[0-9]+$/) {
                    v = t + 0
                    if (v >= -4194304 && v <= -3145729) print line
                } else if (t ~ /^[0-9]+$/ && length(t) == 10) {
                    v = t + 0
                    if (v >= 4290772992 && v <= 4291821567) print line
                }
            }
            next
        }
        $1 == "S" && /Contents of section/ {
            data = ($0 !~ /text|eh_frame|debug|comment|note|unwind|compact/)
            next
        }
        $1 == "S" && data {
            for (i = 3; i <= 6 && i <= NF; i++) {
                w = tolower($i)
                if (length(w) != 8 || w !~ /^[0-9a-f]+$/) continue
                if (substr(w, 7, 2) == "ff" && substr(w, 5, 1) == "c") print "data word " w
            }
        }' | sort -u
    )
    if [ -n "$hits" ]; then
        echo "check-engine: ROM-looking constant (0xFFCxxxxx) in $obj:" >&2
        echo "$hits" | sed 's/^/    /' >&2
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "check-engine: FAILED - the engine must be fixed-point, heap-free and free of 0xFFCxxxxx constants" >&2
    exit 1
fi
echo "check-engine: ok - $# sources, no floating point, no heap, no 0xFFCxxxxx constants"
