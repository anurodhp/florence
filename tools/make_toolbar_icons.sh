#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# Renders the Lucide SVGs in frontend/assets/icons/src into the TIFFs the toolbar draws, one per colour
# state (normal / disabled / pressed) and, for the star, the filled bookmarked look. TIFF because every
# GNUstep reads it. Needs rsvg-convert (librsvg) and ImageMagick's convert; the outputs are committed,
# so the cross build on the Mac needs neither.   Usage: tools/make_toolbar_icons.sh
set -euo pipefail
cd "$(dirname "$0")/../frontend/assets/icons"
SIZE=18             # the toolbar glyph box, in pixels (the 24-unit Lucide grid scaled to it)
STROKE=2            # Lucide's stroke width in grid units: ~1.5 px at 18 px

render() { # name source-svg size stroke-colour fill-colour stroke-width
    local out="$1" src="$2" size="$3" col="$4" fill="$5" sw="$6" tmp
    tmp="$out.tmp.svg"
    sed -e "s/stroke=\"currentColor\"/stroke=\"$col\"/" -e "s/fill=\"none\"/fill=\"$fill\"/" \
        -e "s/stroke-width=\"2\"/stroke-width=\"$sw\"/" "$src" > "$tmp"
    rsvg-convert -w "$size" -h "$size" "$tmp" -o "$out.png"
    convert "$out.png" -alpha on -compress none "$out.tiff"
    rm -f "$tmp" "$out.png"
}

for pair in back:chevron-left forward:chevron-right reload:rotate-cw stop:x plus:plus star:star; do
    name="${pair%%:*}"; src="src/${pair#*:}.svg"
    render "tb-$name-normal"   "$src" $SIZE "#4a4a4a" none $STROKE
    render "tb-$name-disabled" "$src" $SIZE "#b4b4b4" none $STROKE
    render "tb-$name-pressed"  "$src" $SIZE "#111111" none $STROKE
done
render tb-star-filled src/star.svg $SIZE "#e8960c" "#f6b02a" $STROKE     # a bookmarked page
render tb-lock        src/lock.svg 12 "#5a5a5a" none 2.4                 # the padlock in the address bar
render tb-close       src/x.svg    10 "#5a5a5a" none 2.6                 # the close mark on a tab
ls -1 tb-*.tiff | wc -l | sed 's/$/ icons written/'
