#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# Builds tests/pi/<name>.c, an Xlib test program, for the Pi into build/root/usr/local/bin.   scripts/build_pi_xtest.sh screen_watch
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
name="$1"
X11INC="$IOKIT_LIBC/x11/include"
T="$BUILD/obj/xtest_$name"; rm -rf "$T"; mkdir -p "$T"
fl_compile "$T" "$FL_DIR/tests/pi/$name.c" -I "$X11INC" -I "$INC" -std=gnu11
fl_compile_report "$name"
fl_link_exe "$ROOT$PREFIX/bin/$name" "$T" "$SYS/libX11.dylib"
