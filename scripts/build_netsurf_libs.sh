#!/bin/bash
# Builds NetSurf's own libraries as PRIVATE STATIC archives (linked into the one
# Florence executable, not shared with anything else on the image) using each
# library's native buildsystem, cross-compiled. Installed into build/nsroot (not
# build/root: headers, .a files and .pc files are build inputs, never deployed).
#
# Environment, not command-line, variables are used on purpose: the libraries'
# Makefiles assign `CFLAGS := ... $(CFLAGS)`, and a command-line CFLAGS would
# replace that whole line, dropping their own -I flags. -Wno-error because their
# -Werror warning set is written for older compilers than Xcode 12's clang.
#   order (each needs the previous ones): buildsystem, libwapcaplet, libparserutils,
#   libhubbub, libcss, libdom, libnsutils, libnsbmp, libnsgif (libnslog is skipped: its bison grammar needs bison >= 3 and the core builds without it, NETSURF_USE_NSLOG=NO).
# Host-side generators (perl scripts, and build-time C tools in libcss/libdom) run
# with the host compiler: BUILD_CC.
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
NSROOT="$BUILD/nsroot"; mkdir -p "$NSROOT"
AR="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ar"
RANLIB="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ranlib"
export PKG_CONFIG_PATH="$NSROOT/lib/pkgconfig" PKG_CONFIG_LIBDIR="$NSROOT/lib/pkgconfig"
fl_require "$TP/buildsystem/Makefile"

echo "+ buildsystem"
( cd "$TP/buildsystem" && make install PREFIX="$NSROOT" > "$BUILD/nsroot/buildsystem.log" 2>&1 )

for lib in ${NS_LIBS:-libwapcaplet libparserutils libhubbub libcss libdom libnsutils libnsbmp libnsgif}; do
    echo "+ $lib"
    d="$TP/$lib"; fl_require "$d/Makefile"
    # in-tree builds leave build-* dirs; start clean
    rm -rf "$d"/build-*
    ( cd "$d" &&
      env CC="$CLANG -isysroot $SDK -target arm64-apple-ios14.4" AR="$AR" RANLIB="$RANLIB" \
          BUILD_CC="cc" \
          CFLAGS="-O2 -fno-builtin -fno-stack-protector -D_FORTIFY_SOURCE=0 -Wno-error -Wno-nullability-completeness -I$NSROOT/include" \
          LDFLAGS="-L$NSROOT/lib" \
          make -j4 PREFIX="$NSROOT" COMPONENT_TYPE=lib-static install ) > "$NSROOT/$lib.log" 2>&1 || {
        tail -30 "$NSROOT/$lib.log" >&2; echo "error: $lib build failed (log: $NSROOT/$lib.log)" >&2; exit 1; }
    ls "$NSROOT/lib/$lib".a >/dev/null
done
echo "NetSurf libraries installed in $NSROOT:"; ls "$NSROOT/lib"/*.a
