#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# woff2 1.0.2, decoder side only (libwoff2common, libwoff2dec): WebKit's USE_WOFF2 (web fonts) needs libwoff2dec.
# C++17 against the LLVM 20 libc++; brotli from scripts/build_brotli.sh.
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
. tools/meson_cross.sh
SRC="$TP/woff2"; fl_require "$SRC/src/woff2_dec.cc"
[ -f "$LIB/libbrotlidec.1.dylib" ] && [ -f "$LIB/libc++.1.dylib" ] || { echo "error: build brotli and libc++ first" >&2; exit 1; }
CXX="$NEWLD_BINDIR/clang++"
cxx() {  # cxx <outdir> <src.cc>
    local od="$1" f="$2"; mkdir -p "$od"
    "$CXX" -isysroot "$SDK" -target arm64-apple-ios14.4 -std=c++17 -O2 -fno-stack-protector -D_FORTIFY_SOURCE=0 -nostdinc++ -isystem "$INC/c++/v1" \
        -I "$INC" -I "$SRC/include" -I "$SRC/src" -Wno-nullability-completeness -c "$f" -o "$od/$(basename "${f%.cc}").o"
}
O="$BUILD/obj/woff2"; rm -rf "$O"
for f in woff2_common table_tags variable_length; do cxx "$O/common" "$SRC/src/$f.cc"; done
for f in woff2_dec woff2_out; do cxx "$O/dec" "$SRC/src/$f.cc"; done
fl_link_dylib woff2common 1:0:2 "$O/common" -L"$LIB" -lc++ -lflocompat
fl_link_dylib woff2dec 1:0:2 "$O/dec" -L"$LIB" -lwoff2common -lbrotlidec -lbrotlicommon -lc++ -lflocompat
fl_stage_headers woff2 "$SRC"/include/woff2/*.h
fl_pc libwoff2common 1.0.2 "-L$LIB -lwoff2common" "" libbrotlicommon
fl_pc libwoff2dec 1.0.2 "-L$LIB -lwoff2dec" "" "libwoff2common libbrotlidec"
