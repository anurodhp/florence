#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# Autotools libraries for the WebKit 2.54 stack, one invocation each:
#   scripts/build_autotools_lib.sh libgpg-error | libgcrypt | libtasn1
# Xcode 12's clang, the iPhoneOS 14.4 SDK, fixup chains with the newer ld (tools/common.sh), out-of-tree build in
# build/at-<name>, staged into $ROOT; -lflocompat first so the libSystem gaps it fills win over the SDK's stub.
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
. tools/meson_cross.sh      # fl_pc, fl_fix_install_names
which="$1"
scripts/stage_iokit_libs.sh >/dev/null
SRC="$TP/$which"; fl_require "$SRC/configure"
B="$BUILD/at-$which"; rm -rf "$B"; mkdir -p "$B"
# libgpg-error 1.47 installs only gpgrt-config, a target program the build host cannot run, and libgcrypt's configure
# wants a runnable gpg-error-config: a stand-in that reports the staged tree
mkdir -p "$BUILD/gpgerr/bin"
cat >| "$BUILD/gpgerr/bin/gpg-error-config" <<GE
#!/bin/sh
[ "\$1" = --mt ] && shift
case "\$1" in
  --version) echo 1.47 ;;
  --cflags) echo "-I$INC" ;;
  --libs) echo "-L$LIB -lgpg-error" ;;
  --prefix) echo "$BUILD/gpgerr" ;;
  *) ;;
esac
GE
chmod +x "$BUILD/gpgerr/bin/gpg-error-config"
printf '#define __API_AVAILABLE_PLATFORM_iosmac(x) macCatalyst,introduced=x\n#define __API_DEPRECATED_PLATFORM_iosmac(x,y) macCatalyst,introduced=x,deprecated=y\n' >| "$BUILD/flo_availability.h"
CF="-isysroot $SDK -target arm64-apple-ios14.4 -fno-builtin -fno-stack-protector -D_FORTIFY_SOURCE=0 $FL_OPT -fno-common -Wno-nullability-completeness -Wno-deprecated-declarations -include $BUILD/flo_availability.h -I$INC"
LF="-B$NEWLD_BINDIR -Wl,-fixup_chains -L$LIB -lflocompat ${FL_NET_DYLIBS[*]}"
case "$which" in
libgpg-error) opts=(--disable-nls --disable-languages --disable-doc --disable-tests --disable-static) ;;
libgcrypt)    opts=(--disable-doc --disable-static --disable-asm --disable-padlock-support --disable-aesni-support --disable-pclmul-support --disable-sse41-support --disable-drng-support --disable-avx-support --disable-avx2-support --disable-neon-support --disable-arm-crypto-support "--with-libgpg-error-prefix=$BUILD/gpgerr") ;;
libtasn1)     opts=(--disable-doc --disable-static --disable-gcc-warnings --disable-gtk-doc) ;;
*) echo "usage: $0 libgpg-error|libgcrypt|libtasn1" >&2; exit 2 ;;
esac
# shared-library links get the explicit dylib list (owners first, libSystem.B last: the SDK stub would win otherwise and
# fail the bind audit); libtool drops such flags from LDFLAGS, so a CC wrapper appends them (tools/cc_link_wrapper.sh).
export FL_REAL_CC="$CLANG" FL_LINK_TAIL="-L$LIB -lflocompat ${FL_SYS_DYLIBS[0]} ${FL_SYS_DYLIBS[1]} ${FL_SYS_DYLIBS[2]} ${FL_SYS_DYLIBS[3]} ${FL_SYS_DYLIBS[4]} ${FL_SYS_DYLIBS[5]} ${FL_NET_DYLIBS[*]} $SYS/libsystem_m.dylib ${FL_SYS_DYLIBS[6]}"
WRAP="$FL_DIR/tools/cc_link_wrapper.sh"
( cd "$B" && env PKG_CONFIG_LIBDIR="$FL_PKGCFG" PKG_CONFIG_PATH= CC="$WRAP" CPP="$CLANG -E $CF" CFLAGS="$CF" LDFLAGS="$LF" CC_FOR_BUILD=cc CFLAGS_FOR_BUILD="-O1" LDFLAGS_FOR_BUILD="" \
    "$SRC/configure" --host=aarch64-apple-darwin --prefix="$PREFIX" --enable-shared "${opts[@]}" ) > "$B/configure.log" 2>&1 \
    || { tail -25 "$B/configure.log" >&2; echo "error: $which configure failed ($B/configure.log)" >&2; exit 1; }
# only the library directory: the tools next to it are programs, and the build host cannot link those against the port
case "$which" in
libgpg-error) subdirs="src" ;;
libgcrypt)    subdirs="compat cipher random mpi src" ;;
libtasn1)     subdirs="lib" ;;
esac
make -C "$B" -j"$(sysctl -n hw.ncpu)" -k > "$B/make.log" 2>&1 || true
DESTDIR="$ROOT" make -C "$B" -k install > "$B/install.log" 2>&1 || true
ls "$LIB" | grep -E "^lib(gpg-error|gcrypt|tasn1)" | tr '\n' ' '; echo
sed -i '' "s#^prefix=$PREFIX\$#prefix=$ROOT$PREFIX#" "$FL_PKGCFG"/*.pc 2>/dev/null || true
