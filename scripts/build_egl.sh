#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# libEGL for the Pi: EGL 1.4 over Mesa's client-side GLX (compat/egl/flo_egl.c), installed beside libGL in /usr/X11/lib
# (build/root/usr/X11/lib/libEGL.1.dylib, install name /usr/X11/lib/libEGL.1.dylib). Mesa's own EGL cannot be built on this
# system (it wants DRI/DRM/GBM). Also builds tests/pi/egl_smoke.c into build/root/usr/local/bin.
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
X11INC="$IOKIT_LIBC/x11/include"
ANGLE_INC="$TP/webkit/Source/ThirdParty/ANGLE/include"          # Khronos EGL headers
[ -f "$ANGLE_INC/EGL/egl.h" ] || { echo "error: $ANGLE_INC/EGL/egl.h missing -- run setup_third_party.sh" >&2; exit 1; }
[ -f "$SYS/libGL.dylib" ] || { echo "error: $SYS/libGL.dylib missing -- build Mesa in the iokit port first" >&2; exit 1; }
O="$BUILD/obj/egl"; rm -rf "$O"; mkdir -p "$O"
XLIB="$ROOT/usr/X11/lib"; mkdir -p "$XLIB"
fl_compile "$O" "$FL_DIR/compat/egl/flo_egl.c" -I "$X11INC" -I "$ANGLE_INC" -fvisibility=hidden -std=gnu11 -Wall -Wextra
fl_compile_report egl
OUT="$XLIB/libEGL.1.dylib"; rm -f "$OUT" "$XLIB/libEGL.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/X11/lib/libEGL.1.dylib -compatibility_version 1.0.0 -current_version 1.4.0 \
    -Wl,-not_for_dyld_shared_cache -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o "$OUT" "$O"/*.o "$SYS/libGL.dylib" "$SYS/libX11.dylib" "${FL_SYS_DYLIBS[@]}" 2>"$O/link.err" \
    || { grep '^  "' "$O/link.err" | sed 's/,.*//' | sort -u >&2; tail -20 "$O/link.err" >&2; echo "error: libEGL link failed" >&2; exit 1; }
ln -s libEGL.1.dylib "$XLIB/libEGL.dylib"
echo "wrote $OUT"
"$FL_DIR/tools/bind_audit.sh" "$OUT"
# the smoke test: EGL + GLES2 only, as WebKit uses them
T="$BUILD/obj/egl_smoke"; rm -rf "$T"; mkdir -p "$T"
fl_compile "$T" "$FL_DIR/tests/pi/egl_smoke.c" -I "$X11INC" -I "$ANGLE_INC" -std=gnu11
fl_compile_report egl_smoke
fl_link_exe "$ROOT$PREFIX/bin/egl_smoke" "$T" "$XLIB/libEGL.1.dylib" "$SYS/libGLESv2.dylib" "$SYS/libGL.dylib" "$SYS/libX11.dylib"
