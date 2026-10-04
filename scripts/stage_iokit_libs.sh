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
fl_require "$ICU_LIB/libicuuc.dylib" "build the iokit port's ICU (build_icu_target.sh)"
fl_stage_headers . "$IOKIT_LIBC/libffi_include/ffi.h" "$IOKIT_LIBC/libffi_include/ffitarget.h" "$IOKIT_LIBC/libffi_include/ffitarget_arm64.h"
fl_stage_headers . "$IOKIT_LIBC/fontstack/usr_include/zlib.h" "$IOKIT_LIBC/fontstack/usr_include/zconf.h"
fl_stage_headers . "$IOKIT_LIBC/fontstack/usr_include/expat.h" "$IOKIT_LIBC/fontstack/usr_include/expat_external.h"
mkdir -p "$INC/unicode" "$INC/libxml"
cp "$ICU_SRC"/common/unicode/*.h "$ICU_SRC"/i18n/unicode/*.h "$ICU_SRC"/io/unicode/*.h "$INC/unicode/"
cp "$IOKIT_LIBC"/xml2_headers/libxml/*.h "$INC/libxml/"
fl_pc libffi 3.4 "-L$SYS -lffi"
fl_pc zlib 1.2.11 "-L$SYS -lz"
fl_pc expat 2.2.8 "-L$SYS -lexpat"
fl_pc icu-uc 66.1 "-L$ICU_LIB -licuuc -licudata"
fl_pc icu-i18n 66.1 "-L$ICU_LIB -licui18n" "" icu-uc
fl_pc icu-io 66.1 "-L$ICU_LIB -licuio" "" icu-i18n
fl_pc libxml-2.0 2.9.4 "-L$SYS -lxml2" "-I$INC"
fl_pc libpng16 1.6.58 "-L$SYS -lpng16" "-I$X11I/libpng16 -I$X11I"
fl_pc freetype2 24.1.18 "-L$SYS -lfreetype" "-I$X11I/freetype2"
fl_pc fontconfig 2.17.1 "-L$SYS -lfontconfig" "-I$X11I" "freetype2 expat"
fl_pc pixman-1 0.42.2 "-L$SYS -lpixman-1" "-I$X11I/pixman-1"
fl_pc cairo 1.18.4 "-L$SYS -lcairo" "-I$X11I/cairo" "pixman-1 fontconfig freetype2 libpng16 zlib"
fl_pc cairo-ft 1.18.4 "" "" "cairo"
echo "staged $(ls "$FL_PKGCFG" | wc -l) pkg-config files in $FL_PKGCFG"
