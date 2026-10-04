#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# HarfBuzz 8.3.0 (WebKit text shaping; ICU glue on, FreeType on, no Cairo/CoreText/Graphite).
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
. tools/meson_cross.sh
SRC="$TP/harfbuzz"; fl_require "$SRC/meson.build"
scripts/stage_iokit_libs.sh >/dev/null
rm -rf "$SRC/_cross"
fl_meson_build "$SRC" _cross -Dglib=enabled -Dgobject=disabled -Dicu=enabled -Dfreetype=enabled -Dcairo=disabled -Dchafa=disabled \
    -Dgraphite=disabled -Dgraphite2=disabled -Dcoretext=disabled -Dtests=disabled -Dintrospection=disabled -Ddocs=disabled -Dbenchmark=disabled -Dutilities=disabled -Ddoc_tests=false
fl_fix_install_names "$LIB"/libharfbuzz*.0.dylib "$LIB"/libharfbuzz*.dylib 2>/dev/null || true
for f in "$LIB"/libharfbuzz.0.dylib "$LIB"/libharfbuzz-icu.0.dylib; do "$FL_DIR/tools/bind_audit.sh" "$f"; done
