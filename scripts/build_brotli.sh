#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# Brotli 1.1.0 as libbrotlicommon / libbrotlidec / libbrotlienc dylibs (libsoup 3 content decoding, WOFF2 later).
# Hand loop like the iokit port's libraries: its CMake file builds static archives here whatever BUILD_SHARED_LIBS says.
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
. tools/meson_cross.sh
SRC="$TP/brotli"; fl_require "$SRC/c/common/constants.c"
O="$BUILD/obj/brotli"; rm -rf "$O"; mkdir -p "$O"
fl_stage_headers brotli "$SRC"/c/include/brotli/*.h
build_one() {   # build_one <stem> <srcdir> [extra libs]
    local stem="$1" d="$2"; shift 2
    local od="$O/$stem"; mkdir -p "$od"
    for f in "$SRC/c/$d"/*.c; do
        fl_compile "$od" "$f" -DBROTLI_SHARED_COMPILATION -DOS_MACOSX -DBROTLI_HAVE_LOG2=1 -I "$SRC/c/include" -Wno-everything
    done
    fl_compile_report "$stem"
    fl_link_dylib "$stem" 1:1:0 "$od" "$@"
}
build_one brotlicommon common
build_one brotlidec dec -L"$LIB" -lbrotlicommon "${FL_NET_DYLIBS[@]}"
build_one brotlienc enc -L"$LIB" -lbrotlicommon -lflocompat "$SYS/libsystem_m.dylib"
for n in common dec enc; do
    extra=""; [ "$n" != common ] && extra="libbrotlicommon"
    fl_pc "libbrotli$n" 1.1.0 "-L$LIB -lbrotli$n" "" "$extra"
done
