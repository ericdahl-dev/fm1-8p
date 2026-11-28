#!/bin/sh
# Draw the site's share card (og:image, 1200x630) from tools/share-card/card.html into site/share.jpg.
# Needs Chromium and ImageMagick; run from the repository root.
set -eu
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars --allow-file-access-from-files \
  --window-size=1200,630 --screenshot="$tmp/card.png" "file://$PWD/tools/share-card/card.html" >/dev/null 2>&1
magick "$tmp/card.png" -crop 1200x630+0+0 +repage -strip -quality 88 site/share.jpg
