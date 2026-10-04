#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# LLVM 20.1.8's libc++ and libc++abi for the Pi (WebKit 2.54 is C++23; the iokit port ships LLVM 11's).
# Built SIDE BY SIDE first: install name /usr/local/lib/libc++.1.dylib, so nothing already on the Pi changes
# (the system one is on the boot path: libSystem.B, libdyld, launchd link it). Swapping it in as /usr/lib/libc++.1.dylib is
# a separate, audited step (docs/HANDOFF.md). Compiler: the daily Xcode's clang 21 (Xcode 12's clang is too old for
# C++23) with the iPhoneOS 14.4 SDK as sysroot, as the iokit port's WebKit feasibility study did.
#   scripts/build_libcxx.sh [configure|build]
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
[ -f "$LIB/libflocompat.dylib" ] || scripts/build_compat.sh
SRC="$TP/llvm-project"; fl_require "$SRC/runtimes/CMakeLists.txt"
B="$BUILD/libcxx"; mkdir -p "$B"
NEWBIN="$NEWLD_BINDIR"
CFL="-isysroot $SDK -target arm64-apple-ios14.4 -fno-stack-protector -D_FORTIFY_SOURCE=0 $FL_OPT -fno-common -Wno-nullability-completeness -Wno-deprecated-declarations"
LFL="-isysroot $SDK -target arm64-apple-ios14.4 -nostdlib -B$NEWBIN -Wl,-fixup_chains ${FL_SYS_DYLIBS[0]} ${FL_SYS_DYLIBS[1]} ${FL_SYS_DYLIBS[2]} ${FL_SYS_DYLIBS[3]} ${FL_SYS_DYLIBS[4]} ${FL_SYS_DYLIBS[5]} -L$LIB -lflocompat $SYS/libunwind.dylib $SYS/libsystem_m.dylib ${FL_SYS_DYLIBS[6]}"
cat >| "$B/toolchain.cmake" <<TC
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR arm64)
set(CMAKE_C_COMPILER "$NEWBIN/clang")
set(CMAKE_CXX_COMPILER "$NEWBIN/clang++")
set(CMAKE_AR "$NEWBIN/ar")
set(CMAKE_RANLIB "$NEWBIN/ranlib")
set(CMAKE_OSX_SYSROOT "$SDK")
set(CMAKE_OSX_ARCHITECTURES arm64)
set(CMAKE_C_FLAGS_INIT "$CFL")
set(CMAKE_CXX_FLAGS_INIT "$CFL")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_SHARED_LINKER_FLAGS_INIT "$LFL")
TC
if [ "${1:-configure}" != build ] || [ ! -f "$B/build.ninja" ]; then
    rm -f "$B/CMakeCache.txt"    # the *_INIT flags of the toolchain file only apply to a fresh cache
    ( cd "$B" && cmake -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$B/toolchain.cmake" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi" \
        -DLIBCXX_ENABLE_SHARED=ON -DLIBCXX_ENABLE_STATIC=OFF -DLIBCXXABI_ENABLE_SHARED=OFF -DLIBCXXABI_ENABLE_STATIC=ON \
        -DLIBCXX_STATICALLY_LINK_ABI_IN_SHARED_LIBRARY=ON -DLIBCXX_CXX_ABI=libcxxabi \
        -DLIBCXXABI_USE_LLVM_UNWINDER=OFF -DLIBCXX_INCLUDE_TESTS=OFF -DLIBCXX_INCLUDE_BENCHMARKS=OFF -DLIBCXXABI_INCLUDE_TESTS=OFF \
        -DLIBCXX_ENABLE_EXCEPTIONS=ON -DLIBCXX_ENABLE_RTTI=ON -DLIBCXX_ENABLE_THREADS=ON -DLIBCXX_ENABLE_FILESYSTEM=ON \
        -DLIBCXX_HAS_GCC_LIB=OFF -DLIBCXX_HAS_GCC_S_LIB=OFF -DLIBCXXABI_HAS_GCC_LIB=OFF -DLIBCXXABI_HAS_GCC_S_LIB=OFF \
        -DCMAKE_INSTALL_NAME_DIR="$PREFIX/lib" -DCMAKE_BUILD_WITH_INSTALL_NAME_DIR=ON -DLIBCXX_INSTALL_LIBRARY=ON -DLLVM_INCLUDE_TESTS=OFF -DLIBCXX_ENABLE_ASSERTIONS=OFF \
        "$SRC/runtimes" ) > "$B/configure.log" 2>&1 || { tail -25 "$B/configure.log" >&2; echo "error: configure failed (see $B/configure.log)" >&2; exit 1; }
    echo "configured; run: scripts/build_libcxx.sh build"
    exit 0
fi
ninja -C "$B" cxx generate-cxx-headers
DESTDIR="$ROOT" ninja -C "$B" install-cxx install-cxx-headers >| "$B/install.log" 2>&1 || { tail -20 "$B/install.log" >&2; exit 1; }
"$FL_DIR/tools/bind_audit.sh" "$LIB/libc++.1.dylib"
