#!/bin/sh
# #54: the screen draws only in its theme's colors. Every color draw.c uses comes from the theme
# (firmware/ui_map.c's style tables, through T), so a theme switch changes all of it. A color written
# into draw.c itself would stay the same in both themes.
#
# Any HB_RGB(...) or hex number outside a comment fails, wherever it is (a call's argument, a variable,
# a #define, a call's next line), except the few that are not colors (ALLOWED). A plain decimal 0
# (black) is not caught. With a file argument it scans that file instead (e.g. another branch's).
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
draw=${1:-$here/firmware/fm1_8p/draw.c}

# not colors: the RAM and flash ranges a text pointer must be in, a strip id, the roll's all-columns mask
ALLOWED='0x0[12][0-9A-Fa-f]{6}u|GLO_STRIP 0x100|roll_todo = 0xFFFFu'

scan() {
    [ -f "$1" ] || { echo "test_draw_colors.sh: no such file: $1" >&2; return 2; }
    sed -e 's#/\*.*\*/##g' -e 's#/\*.*##' -e 's#^[[:space:]]*\*.*##' "$1" | sed -E "s/$ALLOWED//g" |
        grep -n -E 'HB_RGB\(|0[xX][0-9A-Fa-f]+' || true
}

# the scan itself: each of these forms is a color of draw.c's own and must be found
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
cat > "$tmp" <<'EOF'
H->cv_begin(240, 50, 0x0000);
uint16_t c = 0xF800u;
#define C_OWN 0x1F
CV_TEXT(M, 0, 0, "X",
        HB_RGB(1, 2, 3));
seg7(x, y, s, mix(C_WINDOW, 0x07E0, 20), 0);
EOF
found=$(scan "$tmp" | wc -l | tr -d ' ')
if [ "$found" -ne 5 ]; then
    echo "test_draw_colors.sh: the scan found $found of the sample's 5 colors" >&2
    exit 1
fi

bad=$(scan "$draw") || exit 2
if [ -n "$bad" ]; then
    echo "draw.c draws in a color of its own, not the theme's (#54):" >&2
    echo "$bad" >&2
    exit 1
fi
echo "draw colors: all from the theme"
