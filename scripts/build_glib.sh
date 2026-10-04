#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# GLib 2.66.8 (September 2020, the vintage of the Safari 14 WebKit) as dylibs for the Pi:
# libglib, libgobject, libgmodule, libgthread, libgio, plus the proxy libintl it needs.
# Meson cross build (tools/meson_cross.sh); pcre is GLib's own bundled copy.
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
. tools/meson_cross.sh
SRC="$TP/glib"; fl_require "$SRC/meson.build"
scripts/stage_iokit_libs.sh >/dev/null    # libffi and zlib (and the rest) for pkg-config
rm -rf "$SRC/_cross"
fl_meson_build "$SRC" _cross \
    -Dinternal_pcre=true -Dselinux=disabled -Dlibmount=disabled -Dman=false -Dgtk_doc=false \
    -Dinstalled_tests=false -Dnls=disabled -Dfam=false -Dxattr=false -Dforce_posix_threads=true -Diconv=external -Dbsymbolic_functions=false \
    -Ddtrace=false -Dsystemtap=false -Dgio_module_dir=lib/gio/modules
fl_fix_install_names "$LIB"/lib{glib,gobject,gmodule,gthread,gio,intl}-2.0.0.dylib "$LIB"/libintl.dylib 2>/dev/null || true
for f in "$LIB"/lib{glib,gobject,gmodule,gthread,gio}-2.0.0.dylib; do "$FL_DIR/tools/bind_audit.sh" "$f"; done
