#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# tests/pi/interpose_sync.c as a DYLD_INSERT_LIBRARIES shim (build/root/usr/local/lib/libinterpose_sync.dylib) that logs
# __ulock_wait2 / __psynch_cvwait calls: how libpthread's timed condition-variable wait behaves on the Pi.
#   DYLD_INSERT_LIBRARIES=/usr/local/lib/libinterpose_sync.dylib /usr/local/bin/cond_policy
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
O="$BUILD/obj/interpose"; mkdir -p "$O"
fl_compile "$O" "$FL_DIR/tests/pi/interpose_sync.c" -Wno-everything
fl_compile_report interpose
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib -install_name "$PREFIX/lib/libinterpose_sync.dylib" \
    -B"$NEWLD_BINDIR" -Wl,-fixup_chains -o "$LIB/libinterpose_sync.dylib" "$O"/*.o "${FL_SYS_DYLIBS[@]}" 2>&1 | grep -v "^ld: warn" || true
echo "wrote $LIB/libinterpose_sync.dylib"
