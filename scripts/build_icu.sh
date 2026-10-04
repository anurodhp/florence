#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# ICU 74.2 (WebKit 2.54 needs >= 70.1; the iokit port ships Apple's 66.1) as dylibs for the Pi, installed
# next to the system ones: ICU's symbols carry the major version (u_foo_74), the files are libicu*.74.dylib,
# so both load in one process. Same two-stage recipe as the iokit port's build_icu_host.sh/build_icu_target.sh
# (read their headers: -ffreestanding breaks configure, --disable-tools hides the data subdir, fixup chains).
# C++ is built against the LLVM 20 libc++ of scripts/build_libcxx.sh.
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
. tools/meson_cross.sh
SRC="$TP/icu/source"; fl_require "$SRC/runConfigureICU"
[ -f "$LIB/libc++.1.dylib" ] || { echo "error: build scripts/build_libcxx.sh first" >&2; exit 1; }
[ -f "$LIB/libflocompat.dylib" ] || scripts/build_compat.sh
HOST="$BUILD/icu/host"; TGT="$BUILD/icu/target"; mkdir -p "$HOST" "$TGT"
GMAKE=$(command -v gmake || command -v make)
JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"

# Stage 1: the build tools (genrb, gencnv, pkgdata ...) for the build host
if [ ! -f "$HOST/config/icucross.mk" ]; then
    echo "+ ICU host build"
    ( cd "$HOST" && "$SRC/runConfigureICU" MacOSX --disable-tests --disable-samples --disable-extras --enable-static --disable-shared > configure.log 2>&1 \
        && "$GMAKE" -j"$JOBS" > make.log 2>&1 ) || { tail -20 "$HOST/make.log" >&2; exit 1; }
fi

# Stage 2: the target. configure links test programs, so it gets the plain flags (SDK libSystem); the build proper
# names every dylib explicitly, owners first and libSystem.B last (the SDK's libSystem stub would win for expf & co
# and fail the bind audit).
TARGET_FLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -fno-builtin -fno-stack-protector -D_FORTIFY_SOURCE=0 $FL_OPT"
CXXSTD="-std=c++17 -nostdinc++ -isystem $INC/c++/v1"
LDF="-B$NEWLD_BINDIR -Wl,-fixup_chains -nostdlib++ -L$LIB -lc++ -lflocompat"
LDF_FINAL="-B$NEWLD_BINDIR -Wl,-fixup_chains -nostdlib -nostdlib++ -L$LIB -lc++ -lflocompat ${FL_SYS_DYLIBS[0]} ${FL_SYS_DYLIBS[1]} ${FL_SYS_DYLIBS[2]} ${FL_SYS_DYLIBS[3]} ${FL_SYS_DYLIBS[4]} ${FL_SYS_DYLIBS[5]} $SYS/libsystem_m.dylib ${FL_SYS_DYLIBS[6]}"
cd "$TGT"
if [ ! -f config.status ]; then
    echo "+ ICU target configure"
    CC="$NEWLD_BINDIR/clang $TARGET_FLAGS" CXX="$NEWLD_BINDIR/clang++ $TARGET_FLAGS $CXXSTD" \
    CPPFLAGS="-DU_SHOW_CPLUSPLUS_API=1" LDFLAGS="$LDF" \
        "$SRC/runConfigureICU" MacOSX --host=aarch64-apple-darwin --with-cross-build="$HOST" \
        --prefix="$PREFIX" --disable-extras --disable-tests --disable-samples --disable-tools --disable-icuio --disable-layoutex \
        > configure.log 2>&1 || { tail -30 configure.log >&2; exit 1; }
    # --disable-tools also drops the data subdir (Makefile.in ties DATASUBDIR to TOOLS); packaging uses the host tools anyway
    grep -q '^#DATASUBDIR' Makefile && sed -i '' 's/^#DATASUBDIR/DATASUBDIR/' Makefile || true
fi
# relink every run (objects are kept): a rebuilt libflocompat / libc++ must be picked up, nothing else tracks it
rm -f lib/libicu*.dylib stubdata/libicudt*.dylib
"$GMAKE" -j"$JOBS" LDFLAGS="$LDF_FINAL" DEFAULT_LIBS="" > make.log 2>&1 || { grep -E "error:|Error " make.log | head -20 >&2; echo "error: ICU build failed (see $TGT/make.log)" >&2; exit 1; }
"$GMAKE" install LDFLAGS="$LDF_FINAL" DEFAULT_LIBS="" DESTDIR="$ROOT" > install.log 2>&1 || { tail -20 install.log >&2; exit 1; }

# the installed .pc files name /usr/local (the target); the build host finds the staged tree
sed -i '' "s#^prefix = $PREFIX\$#prefix = $ROOT$PREFIX#; s#^prefix=$PREFIX\$#prefix=$ROOT$PREFIX#" "$LIB"/pkgconfig/icu-*.pc

# ICU names its dylibs by bare file name; the target loads them by absolute path like everything else here
for f in "$LIB"/libicu{uc,i18n,data}.74.dylib; do
    install_name_tool -id "$PREFIX/lib/$(basename "$f")" "$f"
    for dep in $(otool -L "$f" | awk '/^\tlibicu/ {print $1}'); do install_name_tool -change "$dep" "$PREFIX/lib/$dep" "$f"; done
done
for f in "$LIB"/libicu{uc,i18n,data}.74.dylib; do "$FL_DIR/tools/bind_audit.sh" "$f"; done
