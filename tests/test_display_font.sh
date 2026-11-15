#!/bin/sh
# #55: the display font is generated, never edited by hand, and stays within its flash budget.
# render.py --font-only rebuilds docs/ui/mockups/display_font.h from the glyphs' strokes (no Pillow, no
# host font); the checked-in file must match it byte for byte, and its bytes must be 6144 or fewer.
set -eu
PYTHON=${PYTHON:-python3}
here=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
out=$($PYTHON "$here/docs/ui/mockups/render.py" --font-only "$tmp/display_font.h")
echo "$out"
if ! cmp -s "$tmp/display_font.h" "$here/docs/ui/mockups/display_font.h"; then
    echo "display_font.h differs from what render.py --font-only makes (edited by hand, or not regenerated):" >&2
    diff "$here/docs/ui/mockups/display_font.h" "$tmp/display_font.h" | head -20 >&2
    exit 1
fi
bytes=$(echo "$out" | sed -n 's/^display font: \([0-9]*\) bytes$/\1/p')
if [ -z "$bytes" ] || [ "$bytes" -gt 6144 ]; then
    echo "display font: ${bytes:-?} bytes, over the 6144 budget (#55)" >&2
    exit 1
fi
echo "display font: generated, unedited, $bytes of 6144 bytes"
