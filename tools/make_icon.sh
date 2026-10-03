#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# Regenerates frontend/assets/Florence.{png,tiff} from florence.svg (the Florentine giglio).
# Needs rsvg-convert (librsvg) and ImageMagick's convert; the outputs are committed, so the
# cross build on the Mac needs neither.  TIFF because every GNUstep has it; PNG as a fallback.
set -euo pipefail
cd "$(dirname "$0")/../frontend/assets"
# Window Maker draws icons in a 64x64 tile and does not scale what GNUstep hands it: make the image exactly
# that size, with a transparent margin so the artwork does not touch the tile's edge.
rsvg-convert -w 52 -h 52 florence.svg -o Florence.png
convert Florence.png -background none -gravity center -extent 64x64 -alpha on Florence.png
convert Florence.png -alpha on -compress none Florence.tiff
echo "wrote $(pwd)/Florence.png and Florence.tiff"
