#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# tests/pi/fastmem.c as a DYLD_INSERT_LIBRARIES shim (build/root/usr/local/lib/libfastmem.dylib): a fast memset/bzero, to measure what the
# image's slow one costs. DYLD_INSERT_LIBRARIES=/usr/local/lib/libfastmem.dylib /usr/local/bin/fault_bench
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
O="$BUILD/obj/fastmem"; rm -rf "$O"; mkdir -p "$O"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -O2 -fno-builtin -ffreestanding -fno-stack-protector -D_FORTIFY_SOURCE=0 \
    "$FL_DIR/tests/pi/fastmem.c" -o "$O/fastmem.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib -install_name "$PREFIX/lib/libfastmem.dylib" \
    -B"$NEWLD_BINDIR" -Wl,-fixup_chains -o "$LIB/libfastmem.dylib" "$O"/*.o "${FL_SYS_DYLIBS[@]}" 2>&1 | grep -v "^ld: warn" || true
echo "wrote $LIB/libfastmem.dylib"
