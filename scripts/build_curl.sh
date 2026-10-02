#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# Builds libcurl 8.15.0 (third_party/curl release tarball) as a real dylib,
# /usr/local/lib/libcurl.4.dylib, with mbedTLS for https and zlib.
#
# curl's configure runs only to produce an accurate lib/curl_config.h: it is
# cross-run against the iPhoneOS 14.4 SDK (compile and link probes only, no run
# tests). The library itself is then compiled file by file from lib/Makefile.inc's
# own source lists with Florence's flags, and linked -nostdlib against the port's
# dylibs (tools/common.sh). The generated curl_config.h is kept in
# configs/curl/ so a rebuild is reproducible without re-running configure.
#   protocols: http, https, file (ftp too, it costs nothing); the rest are off.
#   resolver:  threaded (getaddrinfo on a pthread), so a slow lookup never blocks
#              the browser's UI thread.
#   trust:     /usr/local/share/florence/cacert.pem (curl.se's Mozilla bundle).
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh

SRC="$TP/curl"
fl_require "$SRC/configure"
fl_require "$LIB/libmbedtls.dylib" "run scripts/build_mbedtls.sh first"
CFGDIR="configs/curl"; mkdir -p "$CFGDIR"
WORK="$BUILD/obj/curl-configure"

if [ ! -f "$CFGDIR/curl_config.h" ] || [ "${FL_RECONFIGURE:-0}" = 1 ]; then
    rm -rf "$WORK"; mkdir -p "$WORK"
    ( cd "$WORK"
      "$SRC/configure" --host=aarch64-apple-darwin --prefix="$PREFIX" \
        CC="$CLANG -isysroot $SDK -target arm64-apple-ios14.4" \
        CFLAGS="-O2 -D_FORTIFY_SOURCE=0 -Wno-nullability-completeness" \
        CPPFLAGS="-I$INC" LDFLAGS="-L$LIB" \
        --with-mbedtls="$ROOT$PREFIX" --with-zlib --without-openssl \
        --with-ca-bundle="$PREFIX/share/florence/cacert.pem" \
        --disable-shared --enable-static --disable-docs --disable-manual \
        --disable-ldap --disable-ldaps --disable-rtsp --disable-dict --disable-telnet \
        --disable-tftp --disable-pop3 --disable-imap --disable-smb --disable-smtp \
        --disable-gopher --disable-mqtt --disable-ntlm --disable-unix-sockets \
        --without-libpsl --without-brotli --without-zstd --without-nghttp2 --without-libidn2 \
        --without-libssh2 --without-librtmp --without-ngtcp2 --without-quiche \
        > configure.log 2>&1 ) || { tail -30 "$WORK/configure.log" >&2; echo "error: curl configure failed" >&2; exit 1; }
    cp "$WORK/lib/curl_config.h" "$CFGDIR/curl_config.h"
    # Edits to the configure result, each for a target fact the SDK probe cannot see:
    #  - HAVE_BUILTIN_AVAILABLE: lib/cf-socket.c:1241 then calls connectx() for TCP
    #    Fast Open, which libsystem_kernel here does not export; undefined, the same
    #    #if falls back to plain connect(2) (cf-socket.c:1259-1261).
    sed -i '' 's|^#define HAVE_BUILTIN_AVAILABLE 1|/* #undef HAVE_BUILTIN_AVAILABLE */|' "$CFGDIR/curl_config.h"
    grep -E "USE_MBEDTLS|HAVE_LIBZ|USE_THREADS_POSIX|CURL_CA_BUNDLE" "$CFGDIR/curl_config.h" || true
fi

# Source lists straight from lib/Makefile.inc.
FILES=$(make -s -f - <<MK
include $SRC/lib/Makefile.inc
all:
	@echo \$(LIB_CFILES) \$(LIB_VAUTH_CFILES) \$(LIB_VTLS_CFILES) \$(LIB_VQUIC_CFILES) \$(LIB_VSSH_CFILES) \$(LIB_CURLX_CFILES)
MK
)
[ -n "$FILES" ] || { echo "error: no sources read from lib/Makefile.inc" >&2; exit 1; }

OBJ="$BUILD/obj/curl"; rm -rf "$OBJ"; mkdir -p "$OBJ"
fl_stage_headers curl "$SRC"/include/curl/*.h
CF=(-DHAVE_CONFIG_H -DBUILDING_LIBCURL -I "$PWD/$CFGDIR" -I "$SRC/lib" -I "$SRC/include")
for f in $FILES; do fl_compile "$OBJ" "$SRC/lib/$f" "${CF[@]}"; done
fl_compile_report libcurl

# libcurl.so.4 = version-info 12:1:8 upstream -> -version-number 4:8:1
fl_link_dylib curl 4:8:1 "$OBJ" "$LIB/libmbedtls.dylib" "$LIB/libmbedx509.dylib" "$LIB/libmbedcrypto.dylib" \
    "$SYS/libz.dylib" "${FL_NET_DYLIBS[@]}" "$SYS/libcopyfile.dylib"   # realpath$DARWIN_EXTSN: libcopyfile
mkdir -p "$ROOT$PREFIX/share/florence"
cp "$TP/ca/cacert.pem" "$ROOT$PREFIX/share/florence/cacert.pem"
