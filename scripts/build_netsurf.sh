#!/bin/bash
# Builds the NetSurf 3.11 core (third_party/netsurf) for a chosen frontend, linked
# against the dylibs from build_curl.sh / build_jpeg.sh, the iokit port's libpng
# and zlib, and the static NetSurf libraries from build_netsurf_libs.sh.
#   scripts/build_netsurf.sh monkey      # headless test frontend (default)
#   scripts/build_netsurf.sh gnustep     # Florence: the GNUstep UI (frontend/ in this repo)
#       needs GNUSTEP_ROOT (prefix with include/ and lib/ of the custom GNUstep and cairo);
#       optional GNUSTEP_OBJCFLAGS, GNUSTEP_LIBS (dylibs, overrides the lib/ search below).
# The tree is copied to build/netsurf-src first (the build writes in-tree).
#
# Configuration (Makefile.config, below): no JavaScript (Duktape off: it is a large
# engine and Florence's first goal is a light renderer), no OpenSSL (TLS is mbedTLS
# inside libcurl; curl.c's OpenSSL certificate-chain code is the WITH_OPENSSL
# option), no libnslog, no utf8proc (IDN only), no webp/jpegxl/psl/video.
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
TARGET_FE="${1:-monkey}"
NSROOT="$BUILD/nsroot"
fl_require "$NSROOT/lib/libcss.a" "run scripts/build_netsurf_libs.sh"
fl_require "$LIB/libcurl.dylib" "run scripts/build_curl.sh"
fl_require "$LIB/libjpeg.dylib" "run scripts/build_jpeg.sh"
X11INC="$IOKIT_LIBC/x11/include"

# pkg-config files for the libraries NetSurf asks pkg-config about but which
# did not come from a pkg-config-aware build.
PC="$NSROOT/lib/pkgconfig"; mkdir -p "$PC"
mkpc() { # name version cflags libs
    printf 'Name: %s\nDescription: %s\nVersion: %s\nCflags: %s\nLibs: %s\n' "$1" "$1" "$2" "$3" "$4" > "$PC/$1.pc"
}
mkpc libcurl 8.15.0 "-I$INC" "-L$LIB -lcurl"
mkpc libjpeg 9.6 "-I$INC" "-L$LIB -ljpeg"
mkpc libpng 1.6.0 "-I$X11INC -I$X11INC/libpng16" "-L$SYS -lpng16"
mkpc zlib 1.2.12 "" "-L$SYS -lz"
# link-time names: -lpng16/-lz resolve to the port's dylibs through these links
LL="$BUILD/linkdir"; rm -rf "$LL"; mkdir -p "$LL"
ln -s "$SYS/libpng16.dylib" "$LL/libpng16.dylib"; ln -s "$SYS/libz.dylib" "$LL/libz.dylib"
ln -s "$LIB/libcurl.dylib" "$LL/libcurl.dylib"; ln -s "$LIB/libjpeg.dylib" "$LL/libjpeg.dylib"
sed -i '' "s#-L$SYS #-L$LL #; s#-L$LIB #-L$LL #" "$PC"/*.pc

W="$BUILD/netsurf-src"
rsync -a --delete --exclude .git "$TP/netsurf/" "$W/"
EXTRA_CFLAGS=""; EXTRA_LIBS=()
if [ "$TARGET_FE" = gnustep ]; then
    : "${GNUSTEP_ROOT:?set GNUSTEP_ROOT to the GNUstep/cairo prefix}"
    rsync -a --delete "$FL_DIR/frontend/" "$W/frontends/gnustep/"
    # resources come from the monkey frontend's res/ (Messages, CSS, icons); the UI draws its own chrome
    mkdir -p "$W/frontends/gnustep/res"; cp -RL "$W/frontends/monkey/res/." "$W/frontends/gnustep/res/"
    OBJCF="${GNUSTEP_OBJCFLAGS:--fobjc-runtime=gnustep-2.0 -fno-objc-arc -fconstant-string-class=NSConstantString -DGNUSTEP -DGNUSTEP_BASE_LIBRARY=1 -DGNU_GUI_LIBRARY=1}"
    CAIROINC="$GNUSTEP_ROOT/include/cairo"; [ -d "$CAIROINC" ] || CAIROINC="$X11INC/cairo"
    EXTRA_CFLAGS="$OBJCF -I$GNUSTEP_ROOT/include -I$CAIROINC -I$X11INC"
    if [ -n "${GNUSTEP_LIBS:-}" ]; then read -r -a EXTRA_LIBS <<< "$GNUSTEP_LIBS"; else
        for l in gnustep-gui gnustep-base objc cairo pixman-1 fontconfig freetype; do
            f=$(ls "$GNUSTEP_ROOT"/lib/lib$l*.dylib 2>/dev/null | head -1 || true)
            [ -n "$f" ] || { echo "error: lib$l*.dylib not under $GNUSTEP_ROOT/lib (set GNUSTEP_LIBS)" >&2; exit 1; }
            EXTRA_LIBS+=("$f")
        done
    fi
fi
cat > "$W/Makefile.config" <<MK
override NETSURF_USE_CURL := YES
override NETSURF_USE_OPENSSL := NO
override NETSURF_USE_DUKTAPE := NO
override NETSURF_USE_NSLOG := NO
override NETSURF_USE_UTF8PROC := NO
override NETSURF_USE_WEBP := NO
override NETSURF_USE_JPEGXL := NO
override NETSURF_USE_NSPSL := NO
override NETSURF_USE_VIDEO := NO
override NETSURF_USE_LIBICONV_PLUG := NO
override NETSURF_USE_HARU_PDF := NO
override NETSURF_USE_BMP := YES
override NETSURF_USE_GIF := YES
override NETSURF_USE_PNG := YES
override NETSURF_USE_JPEG := YES
MK
export PKG_CONFIG_PATH="$PC" PKG_CONFIG_LIBDIR="$PC"
# NetSurf's own link step is skipped on purpose (it links against the host-style
# SDK stubs); the objects it built are linked here with tools/common.sh's recipe.
( cd "$W" && env \
    CC="$CLANG -isysroot $SDK -target arm64-apple-ios14.4" \
    CFLAGS="-O2 -fno-builtin -fno-stack-protector -D_FORTIFY_SOURCE=0 -Wno-error -Wno-nullability-completeness -Wno-deprecated-declarations -D_DARWIN_C_SOURCE -DNDEBUG -I$NSROOT/include $EXTRA_CFLAGS" \
    FLORENCE_CFLAGS="$EXTRA_CFLAGS" LDFLAGS="-L$NSROOT/lib -L$LL" \
    make -k -j4 TARGET="$TARGET_FE" BUILD_CC=cc HOST_CC=cc ) > "$BUILD/netsurf-$TARGET_FE.log" 2>&1 || true
OBJDIR="$W/build/Darwin-$TARGET_FE"
if grep -E "\.[chm]:[0-9]+:[0-9]*:? *(fatal )?error:|\*\*\* .*\.o\]" "$BUILD/netsurf-$TARGET_FE.log" | head -20 | grep .; then
    echo "error: NetSurf ($TARGET_FE) failed to compile, see $BUILD/netsurf-$TARGET_FE.log" >&2; exit 1
fi
NSA=("$NSROOT"/lib/libcss.a "$NSROOT"/lib/libdom.a "$NSROOT"/lib/libhubbub.a "$NSROOT"/lib/libparserutils.a
     "$NSROOT"/lib/libwapcaplet.a "$NSROOT"/lib/libnsutils.a "$NSROOT"/lib/libnsbmp.a "$NSROOT"/lib/libnsgif.a)
fl_link_exe "$ROOT$PREFIX/bin/ns$TARGET_FE" "$OBJDIR" "${NSA[@]}" \
    "$LIB/libcurl.dylib" "$LIB/libjpeg.dylib" "$SYS/libpng16.dylib" "$SYS/libz.dylib" \
    "${EXTRA_LIBS[@]}" "$SYS/libexpat.dylib" "$SYS/libiconv.dylib" "$SYS/libsystem_m.dylib" "${FL_NET_DYLIBS[@]}" "$SYS/libcopyfile.dylib"

# Frontend resources (CSS, messages, icons) where the search path looks:
# ${HOME}/.netsurf/ : ${NETSURFRES} : /usr/local/share/netsurf/ (frontends/monkey/main.c).
RES="$ROOT$PREFIX/share/netsurf"; rm -rf "$RES"; mkdir -p "$RES"
cp -RL "$W/frontends/$TARGET_FE/res/." "$RES/"   # -L: res/ is full of symlinks into ../../../resources
rm -f "$RES/ca-bundle" "$RES/ca-bundle.txt"; cp "$TP/ca/cacert.pem" "$RES/ca-bundle"
echo "resources staged in $RES"
