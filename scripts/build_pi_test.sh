#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# Builds one small test program from tests/pi/ for the Pi into build/root/usr/local/bin and bind-audits it.
#   scripts/build_pi_test.sh glib_smoke [extra link flags...]
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
name="$1"; shift
O="$BUILD/obj/pitest_$name"; rm -rf "$O"; mkdir -p "$O"
fl_compile "$O" "$FL_DIR/tests/pi/$name.c" -I "$INC/glib-2.0" -I "$INC/gio-unix-2.0" -I "$LIB/glib-2.0/include" \
    '-D__API_AVAILABLE_PLATFORM_iosmac(x)=macCatalyst,introduced=x' '-D__API_DEPRECATED_PLATFORM_iosmac(x,y)=macCatalyst,introduced=x,deprecated=y'
fl_compile_report "$name"
fl_link_exe "$ROOT$PREFIX/bin/$name" "$O" -L"$LIB" "$@" "${FL_NET_DYLIBS[@]}"
