#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# Regenerates frontend/assets/Florence.{png,tiff} from florence.svg (the Florentine giglio).
# Needs rsvg-convert (librsvg) and ImageMagick's convert; the outputs are committed, so the
# cross build on the Mac needs neither.  TIFF because every GNUstep has it; PNG as a fallback.
set -euo pipefail
cd "$(dirname "$0")/../frontend/assets"
rsvg-convert -w 128 -h 128 florence.svg -o Florence.png
convert Florence.png -alpha on -compress none Florence.tiff
echo "wrote $(pwd)/Florence.png and Florence.tiff"
