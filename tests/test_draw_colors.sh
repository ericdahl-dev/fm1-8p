#!/bin/sh
# #54: the screen draws only in its theme's colors. Every color draw.c uses comes from the theme
# (firmware/ui_map.c's style tables, through T), so a theme switch changes all of it. A color written
# into draw.c itself (HB_RGB(...), or a 0x literal handed to a text, rect or line call) would stay the
# same in both themes.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
draw=${1:-$here/firmware/fm1_8p/draw.c}
bad=$(grep -n -E 'HB_RGB\(|(CV_TEXT|cv_rect|cv_line|text_c)\(.*0x[0-9A-Fa-f]{4}' "$draw" || true)
if [ -n "$bad" ]; then
    echo "draw.c draws in a color of its own, not the theme's (#54):" >&2
    echo "$bad" >&2
    exit 1
fi
echo "draw colors: all from the theme"
