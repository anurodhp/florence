#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# GLib 2.78.6 (WebKit 2.54 needs >= 2.70) as dylibs for the Pi:
# libglib, libgobject, libgmodule, libgthread, libgio, plus the proxy libintl it needs.
# Meson cross build (tools/meson_cross.sh); PCRE2 from scripts/build_pcre2.sh.
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
. tools/meson_cross.sh
SRC="$TP/glib"; fl_require "$SRC/meson.build"
[ -f "$LIB/pkgconfig/libpcre2-8.pc" ] || scripts/build_pcre2.sh
scripts/stage_iokit_libs.sh >/dev/null    # libffi and zlib (and the rest) for pkg-config
rm -rf "$SRC/_cross"
fl_meson_build "$SRC" _cross \
    -Dselinux=disabled -Dlibmount=disabled -Dman=false -Dgtk_doc=false -Dtests=false -Dinstalled_tests=false \
    -Dnls=disabled -Dxattr=false -Dforce_posix_threads=true -Dbsymbolic_functions=false -Ddtrace=false -Dsystemtap=false \
    -Dsysprof=disabled -Dglib_debug=disabled -Dgio_module_dir=lib/gio/modules
fl_fix_install_names "$LIB"/lib{glib,gobject,gmodule,gthread,gio,intl}-2.0.0.dylib "$LIB"/libintl.dylib 2>/dev/null || true
for f in "$LIB"/lib{glib,gobject,gmodule,gthread,gio}-2.0.0.dylib; do "$FL_DIR/tools/bind_audit.sh" "$f"; done
