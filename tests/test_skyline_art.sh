#!/bin/sh
# The skyline art (Eric, 2026-11-27) is generated, never edited by hand: skyline.py --table rebuilds
# docs/ui/mockups/skyline_art.h from the mockup's own drawing (its seeded towers, no Pillow), and the
# checked-in table must match it byte for byte. So the firmware draws what the mockup PNGs show
# (tests/test_skyline.c checks the pixels the table draws).
set -eu
PYTHON=${PYTHON:-python3}
here=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
$PYTHON "$here/docs/ui/mockups/skyline/skyline.py" --table "$tmp/skyline_art.h"
if ! cmp -s "$tmp/skyline_art.h" "$here/docs/ui/mockups/skyline_art.h"; then
    echo "skyline_art.h differs from what skyline.py --table makes (edited by hand, or not regenerated):" >&2
    diff "$here/docs/ui/mockups/skyline_art.h" "$tmp/skyline_art.h" | head -20 >&2
    exit 1
fi
