#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# Small Meson libraries WebKit needs, one invocation each (tools/meson_cross.sh):
#   scripts/build_meson_lib.sh epoxy | xkbcommon | psl | soup
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
. tools/meson_cross.sh
which="$1"
scripts/deps_fixes.sh
scripts/stage_iokit_libs.sh >/dev/null
case "$which" in
epoxy)      dir=libepoxy;     stems='epoxy.0';          opts=(-Degl=no -Dglx=no -Dx11=false -Dtests=false) ;;
xkbcommon)  dir=libxkbcommon; stems='xkbcommon.0';      opts=(-Denable-tools=false -Denable-wayland=false -Denable-x11=false -Denable-docs=false -Denable-xkbregistry=false -Denable-bash-completion=false -Dxkb-config-root=/usr/local/share/X11/xkb -Dx-locale-root=/usr/local/share/X11/locale) ;;
psl)        dir=libpsl;       stems='psl.5';            opts=(-Druntime=libicu -Dbuiltin=true -Dtests=false -Ddocs=false) ;;
soup)       dir=libsoup;      stems='soup-3.0.0';       opts=(-Dgssapi=disabled -Dntlm=disabled -Dbrotli=enabled -Dtls_check=false -Dintrospection=disabled -Dvapi=disabled -Ddocs=disabled -Ddoc_tests=false -Dtests=false -Dautobahn=disabled -Dinstalled_tests=false -Dsysprof=disabled -Dfuzzing=disabled -Dpkcs11_tests=disabled) ;;
*) echo "usage: $0 epoxy | xkbcommon | psl | soup" >&2; exit 2 ;;
esac
SRC="$TP/$dir"; fl_require "$SRC/meson.build"
rm -rf "$SRC/_cross"
case "$which" in xkbcommon|psl|soup) export FL_NINJA_KEEP_GOING=1   ;; esac   # test and fuzz programs that cannot link here (clock(), system() are not on this target)
fl_meson_build "$SRC" _cross "${opts[@]}"
for s in $stems; do
    fl_fix_install_names "$LIB"/lib$s*.dylib 2>/dev/null || true
done
fl_fix_install_names $(ls "$LIB"/lib*.dylib | grep -v -E "libflocompat|libjpeg|libmbed|libcurl") 2>/dev/null || true
for s in $stems; do "$FL_DIR/tools/bind_audit.sh" "$LIB/lib$s.dylib"; done
