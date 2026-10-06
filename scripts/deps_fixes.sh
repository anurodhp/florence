#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# The few edits the dependency sources need on this target. Idempotent; each asserts its anchor, so a
# different upstream stops the build instead of building something else (same pattern as webkit_fixes.sh).
# Under the iokit port's rule these become commits in anurodhp/* forks.
set -euo pipefail
cd "$(dirname "$0")/.."
# edit <file> <anchor> <replacement> <why>
edit() {
    local f="$1" old="$2" new="$3"
    [ -f "$f" ] || { echo "error: $f missing" >&2; exit 1; }
    python3 - "$f" "$old" "$new" <<'PY'
import sys
f, old, new = sys.argv[1:4]
s = open(f).read()
if new in s:                      # exact (multi-line) containment: already applied
    sys.exit(0)
if old not in s:
    sys.exit("error: anchor not found in %s: %s" % (f, old))
open(f, 'w').write(s.replace(old, new, 1))
print("edited " + f)
PY
}
# libepoxy 1.5.4 src/meson.build:58 passes "-compatibility_version 1" as ONE argument; ld64 rejects the space.
edit third_party/libepoxy/src/meson.build \
    "'-compatibility_version 1', '-current_version 1.0'," \
    "'-Wl,-compatibility_version,1', '-Wl,-current_version,1.0',"
# libepoxy has no EGL (or working GLES 2) library name for Darwin: it assumes macOS has no EGL. This system's Mesa libraries are in
# /usr/X11/lib, and libEGL is Florence's EGL over GLX (compat/egl). Full paths, because dlopen of a bare name does not look there.
edit third_party/libepoxy/src/dispatch_common.c \
    '#define GLES2_LIB "libGLESv2.so"
#elif defined(__ANDROID__)' \
    '#define EGL_LIB "/usr/X11/lib/libEGL.1.dylib" /* Florence: Mesa GLX presenter + compat/egl */
#define GLES2_LIB "/usr/X11/lib/libGLESv2.2.dylib"
#elif defined(__ANDROID__)'
edit third_party/libepoxy/src/dispatch_common.h \
    '#define PLATFORM_HAS_EGL 0 ' \
    '#define PLATFORM_HAS_EGL ENABLE_EGL /* Florence: libEGL is compat/egl */'
