#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# OpenSSL 3.0.15 (LTS) as libcrypto/libssl dylibs for the Pi: the TLS library glib-networking's GIO module (which libsoup and so
# WebKit load for https) is built on. Libraries only (make build_libs: the openssl program cannot be linked here), no engines/ASYNC/legacy.
# CA roots: OPENSSLDIR is /usr/local/ssl, the trust file is /usr/local/ssl/cert.pem (staged from third_party/ca/cacert.pem).
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
. tools/meson_cross.sh
SRC="$TP/openssl"; fl_require "$SRC/Configure"
B="$BUILD/at-openssl"; rm -rf "$B"; mkdir -p "$B"
printf '#define __API_AVAILABLE_PLATFORM_iosmac(x) macCatalyst,introduced=x\n#define __API_DEPRECATED_PLATFORM_iosmac(x,y) macCatalyst,introduced=x,deprecated=y\n' >| "$BUILD/flo_availability.h"
# shared-library links get the explicit dylib list (owners first): tools/cc_link_wrapper.sh
export FL_REAL_CC="$CLANG" FL_LINK_TAIL="-B$NEWLD_BINDIR -Wl,-fixup_chains -L$LIB -lflocompat ${FL_SYS_DYLIBS[0]} ${FL_SYS_DYLIBS[1]} ${FL_SYS_DYLIBS[2]} ${FL_SYS_DYLIBS[3]} ${FL_SYS_DYLIBS[4]} ${FL_SYS_DYLIBS[5]} ${FL_NET_DYLIBS[*]} $SYS/libsystem_m.dylib ${FL_SYS_DYLIBS[6]}"
WRAP="$FL_DIR/tools/cc_link_wrapper.sh"
( cd "$B" && env CC="$WRAP" AR="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ar" RANLIB="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ranlib" \
    perl "$SRC/Configure" darwin64-arm64-cc shared no-tests no-async no-engine no-dso no-legacy no-comp no-ui-console no-afalgeng no-module \
    --prefix="$PREFIX" --openssldir="$PREFIX/ssl" --libdir=lib \
    "-isysroot$SDK" --target=arm64-apple-ios14.4 -fno-builtin -fno-stack-protector -D_FORTIFY_SOURCE=0 "$FL_OPT" -fno-common \
    -Wno-nullability-completeness -Wno-deprecated-declarations "-include$BUILD/flo_availability.h" ) > "$B/configure.log" 2>&1 \
    || { tail -20 "$B/configure.log" >&2; echo "error: openssl configure failed ($B/configure.log)" >&2; exit 1; }
make -C "$B" -j"$(sysctl -n hw.ncpu)" build_libs > "$B/make.log" 2>&1 || { grep -E "error:|Error |Undefined|^  \"_" "$B/make.log" | head -15 >&2; echo "error: openssl build failed ($B/make.log)" >&2; exit 1; }
# OpenSSL ignores DESTDIR from the environment (the first run installed into the build host's /usr/local): pass it as a make
# variable, and look at where the install would write before running it.
dry="$(make -C "$B" -n install_sw DESTDIR="$ROOT" 2>/dev/null || true)"
case "$dry" in *"$ROOT"*) ;; *) { echo "error: the install would not go to $ROOT, refusing to run it" >&2; exit 1; } ;; esac
make -C "$B" install_sw DESTDIR="$ROOT" > "$B/install.log" 2>&1 || { tail -15 "$B/install.log" >&2; exit 1; }
[ -f "$LIB/libcrypto.3.dylib" ] || { echo "error: libcrypto.3.dylib is not in $LIB after the install" >&2; exit 1; }
sed -i '' "s#^prefix=$PREFIX\$#prefix=$ROOT$PREFIX#" "$FL_PKGCFG"/openssl.pc "$FL_PKGCFG"/libssl.pc "$FL_PKGCFG"/libcrypto.pc 2>/dev/null || true
# the openssl program and c_rehash are not wanted on the Pi (the program is not linked against the iokit port's libraries)
rm -f "$ROOT$PREFIX/bin/openssl" "$ROOT$PREFIX/bin/c_rehash"
# the CA roots the openssl GIO backend (and OpenSSL's default store) read
mkdir -p "$ROOT$PREFIX/ssl"; cp "$TP/ca/cacert.pem" "$ROOT$PREFIX/ssl/cert.pem"
for f in "$LIB"/libcrypto.3.dylib "$LIB"/libssl.3.dylib; do "$FL_DIR/tools/bind_audit.sh" "$f"; echo "dynamic lookups in $(basename "$f"): $(nm -m "$f" | grep -c 'dynamically looked up')"; done
