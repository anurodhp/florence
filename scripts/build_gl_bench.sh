#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# Builds tests/pi/gl_readback_bench.c (the cost of the pieces of a GL frame on the Pi) into build/root/usr/local/bin.
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
X11INC="$IOKIT_LIBC/x11/include"
XLIB="$ROOT/usr/X11/lib"
T="$BUILD/obj/gl_readback_bench"; rm -rf "$T"; mkdir -p "$T"
fl_compile "$T" "$FL_DIR/tests/pi/gl_readback_bench.c" -I "$X11INC" -I "$INC" -std=gnu11
fl_compile_report gl_readback_bench
fl_link_exe "$ROOT$PREFIX/bin/gl_readback_bench" "$T" "$XLIB/libEGL.1.dylib" "$SYS/libGLESv2.dylib" "$SYS/libGL.dylib" "$SYS/libX11.dylib"
