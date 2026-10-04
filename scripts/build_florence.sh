#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# Builds the Florence application: frontend/flo_*.c (the engine glue, plain C against WPE WebKit and
# GLib) and frontend/Flo*.m (the GNUstep UI), linked against the engine from scripts/build_webkit.sh.
#
#   scripts/build_florence.sh host   # Linux: build/florence-host/florence, linked against build/webkit-host/root.
#                                    # Needs gnustep-base/gui dev packages (gcc -x objective-c) and `host` WebKit built.
#                                    # Run it under an X server (Xvfb is enough); see docs/webkit-port.md.
#   scripts/build_florence.sh pi     # the iokit toolchain: build/root/Applications/Florence.app. NOT YET WORKING:
#                                    # needs the engine for the Pi (scripts/build_webkit.sh cross). Written to the
#                                    # recipe of the NetSurf build it replaces; never run.
set -euo pipefail
cd "$(dirname "$0")/.."
FL_DIR="$PWD"
MODE="${1:-host}"
SRC="$FL_DIR/frontend"
CFILES=(flo_engine.c flo_platform.c)
MFILES=(FloGLib.m FloPageView.m FloWindow.m FloMain.m)

case "$MODE" in
host)
    B="$FL_DIR/build/florence-host"; mkdir -p "$B"
    WK="${WEBKIT_ROOT:-$FL_DIR/build/webkit-host/root}"
    [ -f "$WK/lib/pkgconfig/wpe-webkit-2.0.pc" ] || { echo "error: $WK has no WPE WebKit -- run scripts/build_webkit.sh host" >&2; exit 1; }
    export PKG_CONFIG_PATH="$WK/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    PC="wpe-webkit-2.0 wpe-platform-2.0 glib-2.0"
    CF="$(pkg-config --cflags $PC)"
    OBJCFLAGS="$(gnustep-config --objc-flags | sed 's/-MMD -MP//') -Wno-expansion-to-defined"
    rm -f "$B"/*.o
    for c in "${CFILES[@]}"; do
        # shellcheck disable=SC2086
        gcc -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter $CF -c "$SRC/$c" -o "$B/${c%.c}.o"
    done
    for m in "${MFILES[@]}"; do
        # shellcheck disable=SC2086
        gcc -x objective-c -O2 -Wall -Wextra -Wno-unused-parameter $OBJCFLAGS -I"$SRC" -c "$SRC/$m" -o "$B/${m%.m}.o"
    done
    # shellcheck disable=SC2046
    gcc "$B"/*.o -o "$B/florence" $(gnustep-config --gui-libs) $(pkg-config --libs $PC) -Wl,-rpath,"$WK/lib"
    echo "wrote $B/florence (WEBKIT_EXEC_PATH=$WK/libexec/wpe-webkit-2.0 if the helper processes are not found)"
    ;;
pi)
    . tools/common.sh
    DEPS="${FL_DEPS_ROOT:-$BUILD/depsroot}"
    fl_require "$LIB/libWPEWebKit-2.0.dylib" "run scripts/build_webkit.sh cross"
    # the engine's dylibs, owners first (the bind audit's rule): what flo_*.c and the UI call directly
    FL_GS_OWNERS=("$LIB/libWPEWebKit-2.0.dylib" "$LIB/libglib-2.0.dylib" "$LIB/libgobject-2.0.dylib" "$LIB/libgio-2.0.dylib")
    . tools/gnustep_env.sh
    OBJ="$BUILD/obj/florence"; rm -rf "$OBJ"; mkdir -p "$OBJ"
    WKINC="-I$DEPS/include/wpe-webkit-2.0 -I$DEPS/include/glib-2.0 -I$DEPS/lib/glib-2.0/include"
    for c in "${CFILES[@]}"; do fl_compile "$OBJ" "$SRC/$c" $WKINC; done
    for m in "${MFILES[@]}"; do
        # shellcheck disable=SC2086
        fl_compile "$OBJ" "$SRC/$m" -I"$SRC" $FL_GS_OBJCFLAGS $FL_OPT
    done
    fl_compile_report florence
    fl_link_gs_exe "$ROOT$PREFIX/bin/florence" "$OBJ"
    # a GNUstep application bundle in the Mac layout the image uses (/Applications/X.app)
    APP="$ROOT/Applications/Florence.app"; rm -rf "$APP"; mkdir -p "$APP/Resources"
    cp "$ROOT$PREFIX/bin/florence" "$APP/Florence"
    cp "$SRC/assets/Florence.tiff" "$SRC/assets/Florence.png" "$APP/Resources/"
    FL_VERSION="$(tr -d '[:space:]' < "$FL_DIR/VERSION")"
    printf '{\n    ApplicationName = Florence;\n    ApplicationDescription = "WebKit with a GNUstep UI";\n    ApplicationRelease = "%s";\n    NSExecutable = Florence;\n    NSIcon = "Florence.tiff";\n    NSPrincipalClass = NSApplication;\n    CFBundleIdentifier = "org.florence.browser";\n}\n' "$FL_VERSION" > "$APP/Resources/Info-gnustep.plist"
    echo "bundle $APP (run: openapp Florence, under an X server)"
    ;;
*) echo "usage: $0 host|pi" >&2; exit 2 ;;
esac
