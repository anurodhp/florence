#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# Builds tests/smoke_engine.c with the engine glue (flo_engine.c, flo_platform.c) for the Pi against the WebKit that
# scripts/build_webkit.sh cross installed in build/root: the same end-to-end check scripts/test_host.sh runs on Linux
# (a frame arrives with the right pixels, title/address events, a click, a wheel), no UI. Run it on the Pi:
#   scripts/build_pi_smoke.sh && scripts/deploy_pi_webkit.sh && ssh root@PI 'cd /var/root && FLO_... smoke_engine page.html datadir'
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
. tools/meson_cross.sh
sed -i '' "s#^prefix=$PREFIX\$#prefix=$ROOT$PREFIX#" "$FL_PKGCFG"/wpe-*.pc 2>/dev/null || true
PC="wpe-webkit-2.0 wpe-platform-2.0 glib-2.0 gobject-2.0 gio-2.0"
CFL=$(env PKG_CONFIG_LIBDIR="$FL_PKGCFG" PKG_CONFIG_PATH= pkg-config --cflags $PC)
O="$BUILD/obj/pi_smoke"; rm -rf "$O"; mkdir -p "$O"
for f in tests/smoke_engine.c frontend/flo_engine.c frontend/flo_platform.c; do
    # shellcheck disable=SC2086
    fl_compile "$O" "$FL_DIR/$f" $CFL -I "$FL_DIR/frontend" -std=gnu11 -Wno-everything \
        '-D__API_AVAILABLE_PLATFORM_iosmac(x)=macCatalyst,introduced=x' '-D__API_DEPRECATED_PLATFORM_iosmac(x,y)=macCatalyst,introduced=x,deprecated=y'
done
fl_compile_report pi_smoke
fl_link_exe "$ROOT$PREFIX/bin/smoke_engine" "$O" -L"$LIB" -lWPEWebKit-2.0 -lgio-2.0 -lgobject-2.0 -lglib-2.0 -lintl -lflocompat "${FL_NET_DYLIBS[@]}"
