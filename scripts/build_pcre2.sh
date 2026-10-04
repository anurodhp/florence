#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# PCRE2 10.42 (GLib 2.78 needs it; GLib 2.66 carried its own PCRE 1) as libpcre2-8.dylib.
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
. tools/cmake_cross.sh
SRC="$TP/pcre2"; fl_require "$SRC/CMakeLists.txt"
fl_cmake_build pcre2 "$SRC" -DBUILD_SHARED_LIBS=ON -DBUILD_STATIC_LIBS=OFF -DPCRE2_BUILD_PCRE2_8=ON -DPCRE2_BUILD_PCRE2_16=OFF \
    -DPCRE2_BUILD_PCRE2_32=OFF -DPCRE2_BUILD_TESTS=OFF -DPCRE2_BUILD_PCRE2GREP=OFF -DPCRE2_SUPPORT_JIT=OFF -DPCRE2_SUPPORT_UNICODE=ON
ls "$LIB" | grep pcre2
"$FL_DIR/tools/bind_audit.sh" "$LIB/libpcre2-8.0.dylib"
