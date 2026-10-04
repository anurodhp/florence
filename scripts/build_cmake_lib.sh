#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# Small CMake libraries for the WebKit 2.54 stack, one invocation each (tools/cmake_cross.sh):
#   scripts/build_cmake_lib.sh libxml2 | nghttp2 | libwebp
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
. tools/cmake_cross.sh
which="$1"
scripts/stage_iokit_libs.sh >/dev/null
case "$which" in
libxml2)  dir=libxml2; audit='xml2.2.9.14';        opts=(-DBUILD_SHARED_LIBS=ON -DLIBXML2_WITH_PYTHON=OFF -DLIBXML2_WITH_LZMA=OFF -DLIBXML2_WITH_ICONV=ON -DLIBXML2_WITH_ICU=OFF -DLIBXML2_WITH_ZLIB=ON -DLIBXML2_WITH_TESTS=OFF -DLIBXML2_WITH_PROGRAMS=OFF -DLIBXML2_WITH_HTTP=OFF -DLIBXML2_WITH_FTP=OFF -DLIBXML2_WITH_MODULES=OFF -DHAVE_SHLLOAD=OFF -DZLIB_LIBRARY="$SYS/libz.dylib" -DZLIB_INCLUDE_DIR="$INC" -DIconv_LIBRARY="$SYS/libiconv.dylib" -DIconv_INCLUDE_DIR="$SDK/usr/include") ;;
nghttp2)  dir=nghttp2;  audit='nghttp2.14';   opts=(-DBUILD_SHARED_LIBS=ON -DBUILD_STATIC_LIBS=OFF -DENABLE_LIB_ONLY=ON -DENABLE_DOC=OFF -DENABLE_FAILMALLOC=OFF) ;;
libwebp)  dir=libwebp;  audit='webp.7 webpdemux.2 sharpyuv.0'; opts=(-DBUILD_SHARED_LIBS=ON -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF) ;;
*) echo "usage: $0 libxml2|nghttp2|libwebp" >&2; exit 2 ;;
esac
[ "$which" = brotli ] && export FL_CMAKE_POLICY_MIN=   # its own minimum (3.15) changes how BUILD_SHARED_LIBS is read under the forced 3.5
SRC="$TP/$dir"; fl_require "$SRC/CMakeLists.txt"
fl_cmake_build "$which" "$SRC" "${opts[@]}"
for s in $audit; do "$FL_DIR/tools/bind_audit.sh" "$LIB/lib$s.dylib"; done
