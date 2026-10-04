#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# Helper for the build_*.sh scripts of CMake projects. SOURCED after tools/common.sh, never run.
#   fl_cmake_build <name> <srcdir> [cmake options...]
#     configure + build + install into $ROOT (DESTDIR) with the toolchain of tools/common.sh: Xcode 12's clang for C
#     (the iokit port's recipe), -nostdlib against its dylibs (owners first, libSystem.B last, libflocompat), the
#     daily Xcode's clang++ with the LLVM 20 libc++ (scripts/build_libcxx.sh) for C++. pkg-config sees only $LIB/pkgconfig.
#   Env: FL_CMAKE_C_CLANG=daily  to compile C with the newer clang too.
FL_PKGCFG="${FL_PKGCFG:-$LIB/pkgconfig}"; mkdir -p "$FL_PKGCFG"
fl_cmake_toolchain() {
    # libffi.h and a few others use the macCatalyst spelling the Xcode-12 SDK does not define; as a header, since
    # parentheses in -D flags do not survive CMake's flag strings
    printf '#define __API_AVAILABLE_PLATFORM_iosmac(x) macCatalyst,introduced=x\n#define __API_DEPRECATED_PLATFORM_iosmac(x,y) macCatalyst,introduced=x,deprecated=y\n' >| "$BUILD/flo_availability.h"
    local tc="$1" cc="${FL_CMAKE_C_CLANG:+$NEWLD_BINDIR/clang}"; cc="${cc:-$CLANG}"
    local cflags="-isysroot $SDK -target arm64-apple-ios14.4 -fno-builtin -fno-stack-protector -D_FORTIFY_SOURCE=0 $FL_OPT -fno-common -Wno-nullability-completeness -Wno-deprecated-declarations -include $BUILD/flo_availability.h"
    local sysl="${FL_SYS_DYLIBS[0]} ${FL_SYS_DYLIBS[1]} ${FL_SYS_DYLIBS[2]} ${FL_SYS_DYLIBS[3]} ${FL_SYS_DYLIBS[4]} ${FL_SYS_DYLIBS[5]} ${FL_NET_DYLIBS[*]} $SYS/libsystem_notify.dylib $SYS/libsystem_m.dylib ${FL_SYS_DYLIBS[6]}"
    local lflags="-isysroot $SDK -target arm64-apple-ios14.4 -nostdlib -B$NEWLD_BINDIR -Wl,-fixup_chains -L$LIB -lflocompat"
    cat >| "$tc" <<TC
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR arm64)
set(CMAKE_C_COMPILER "$cc")
set(CMAKE_CXX_COMPILER "$NEWLD_BINDIR/clang++")
set(CMAKE_AR "$NEWLD_BINDIR/ar")
set(CMAKE_RANLIB "$NEWLD_BINDIR/ranlib")
set(CMAKE_OSX_SYSROOT "$SDK")
set(CMAKE_OSX_ARCHITECTURES arm64)
set(CMAKE_C_FLAGS_INIT "$cflags -I$INC")
set(CMAKE_CXX_FLAGS_INIT "$cflags -std=c++17 -nostdinc++ -isystem $INC/c++/v1 -I$INC")
set(CMAKE_CXX_STANDARD_LIBRARIES "-nostdlib++ -lc++")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_SHARED_LINKER_FLAGS_INIT "$lflags $sysl")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "$lflags $sysl")
set(CMAKE_EXE_LINKER_FLAGS_INIT "$lflags $sysl")
set(CMAKE_INSTALL_NAME_DIR "$PREFIX/lib")
set(CMAKE_BUILD_WITH_INSTALL_NAME_DIR ON)
set(CMAKE_FIND_ROOT_PATH "$ROOT;$BUILD/iokit")
set(CMAKE_PREFIX_PATH "$PREFIX")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_FRAMEWORK NEVER)
TC
}
fl_cmake_build() {
    local name="$1" src="$2"; shift 2
    local b="$BUILD/cmake-$name"; rm -rf "$b"; mkdir -p "$b"
    fl_cmake_toolchain "$b/toolchain.cmake"
    ( cd "$b" && env PKG_CONFIG_LIBDIR="$FL_PKGCFG" PKG_CONFIG_PATH= cmake -GNinja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$b/toolchain.cmake" -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib ${FL_CMAKE_POLICY_MIN-"-DCMAKE_POLICY_VERSION_MINIMUM=3.5"} "$@" "$src" ) \
        > "$b/configure.log" 2>&1 || { tail -25 "$b/configure.log" >&2; echo "error: $name configure failed ($b/configure.log)" >&2; return 1; }
    ninja -C "$b" ${FL_NINJA_KEEP_GOING:+-k 0} > "$b/build.log" 2>&1 || [ -n "${FL_NINJA_KEEP_GOING:-}" ] || { grep -E "error:|FAILED|Undefined|^  \"_" "$b/build.log" | head -20 >&2; echo "error: $name build failed ($b/build.log)" >&2; return 1; }
    DESTDIR="$ROOT" cmake --install "$b" > "$b/install.log" 2>&1 || { tail -15 "$b/install.log" >&2; return 1; }
    # the installed .pc files name /usr/local (the target); the build host finds the staged tree
    sed -i '' "s#^prefix=$PREFIX\$#prefix=$ROOT$PREFIX#" "$FL_PKGCFG"/*.pc 2>/dev/null || true
}
