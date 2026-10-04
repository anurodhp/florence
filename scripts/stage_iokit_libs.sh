#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# Makes the libraries the iokit port already ships findable by pkg-config (the build scripts' only
# way to find anything): headers into build/root/usr/local/include, a .pc file for each. Idempotent.
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
. tools/meson_cross.sh
X11I="$IOKIT_LIBC/x11/include"
ICU_SRC="$IOKIT_DIR/third_party/ICU/icuSources"
ICU_LIB="$IOKIT_DIR/tools/userland_staging/icu_build/target/lib"
fl_stage_headers . "$IOKIT_LIBC/libffi_include/ffi.h" "$IOKIT_LIBC/libffi_include/ffitarget.h" "$IOKIT_LIBC/libffi_include/ffitarget_arm64.h"
fl_stage_headers . "$IOKIT_LIBC/fontstack/usr_include/zlib.h" "$IOKIT_LIBC/fontstack/usr_include/zconf.h"
fl_stage_headers . "$IOKIT_LIBC/fontstack/usr_include/expat.h" "$IOKIT_LIBC/fontstack/usr_include/expat_external.h"
# libproc.h: GLib 2.78 gspawn uses proc_pidinfo (exported by libsystem_kernel) to close fds; the SDK has no libproc.h
fl_stage_headers . "$FL_DIR/compat/include/libproc.h"
fl_stage_headers sys "$FL_DIR/compat/include/sys/proc_info.h" "$FL_DIR/compat/include/sys/random.h"
# ICU: not staged here; this branch builds its own 74 (scripts/build_icu.sh, headers + pkg-config files included)
fl_pc libffi 3.4 "-L$SYS -lffi"
fl_pc zlib 1.2.11 "-L$SYS -lz"
fl_pc expat 2.2.8 "-L$SYS -lexpat"
# libxml2: this branch builds its own 2.9.14 (scripts/build_cmake_lib.sh libxml2); the iokit port has 2.9.4
fl_pc libpng16 1.6.58 "-L$SYS -lpng16" "-I$X11I/libpng16 -I$X11I"
fl_pc freetype2 24.1.18 "-L$SYS -lfreetype" "-I$X11I/freetype2"
fl_pc fontconfig 2.17.1 "-L$SYS -lfontconfig" "-I$X11I" "freetype2 expat"
fl_pc pixman-1 0.42.2 "-L$SYS -lpixman-1" "-I$X11I/pixman-1"
fl_pc cairo 1.18.4 "-L$SYS -lcairo" "-I$X11I/cairo" "pixman-1 fontconfig freetype2 libpng16 zlib"
fl_pc cairo-ft 1.18.4 "" "" "cairo"
echo "staged $(ls "$FL_PKGCFG" | wc -l) pkg-config files in $FL_PKGCFG"
