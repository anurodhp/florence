#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# Builds WPE WebKit (third_party/webkit, pinned by setup_third_party.sh) with the option set in
# config/webkit-options.cmake: the interpreter only, Skia on the CPU, no GPU, no media, no extras.
#
#   scripts/build_webkit.sh host     # Linux: the same engine built with the host compiler, to develop and
#                                    # test the UI and the glue on a PC. Installs into build/webkit-host/root.
#                                    # Needs the host's dev packages (glib, libsoup-3.0, harfbuzz, icu, jpeg,
#                                    # epoxy, gcrypt, tasn1, xkbcommon, libxml2, png, sqlite3, webp, freetype,
#                                    # fontconfig), cmake, ninja, ruby, perl, python3, gperf, unifdef.
#   scripts/build_webkit.sh cross    # the Pi (iokit port toolchain): NOT YET WORKING -- configures only, and
#                                    # stops at the first missing dependency, listing what the port still needs
#                                    # to provide (see docs/webkit-port.md).
#   JOBS=n    parallel compile jobs (default: all cores; a WebCore unified source needs ~1 GB of RAM each)
set -euo pipefail
cd "$(dirname "$0")/.."
FL_DIR="$PWD"
MODE="${1:-host}"
WK="$FL_DIR/third_party/webkit"
BUILD="$FL_DIR/build"
[ -f "$WK/CMakeLists.txt" ] || { echo "error: $WK missing -- run ./setup_third_party.sh" >&2; exit 1; }
scripts/check_webkit_options.sh

case "$MODE" in
host)
    JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}"
    B="$BUILD/webkit-host"; mkdir -p "$B"
    command -v ninja >/dev/null && command -v cmake >/dev/null || { echo "error: cmake and ninja are needed" >&2; exit 1; }
    CC_="${CC:-clang}"; CXX_="${CXX:-clang++}"
    ( cd "$B" && cmake -GNinja -DPORT=WPE -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="$CC_" -DCMAKE_CXX_COMPILER="$CXX_" \
        -DCMAKE_INSTALL_PREFIX="$B/root" -DCMAKE_INSTALL_LIBDIR=lib \
        -C "$FL_DIR/config/webkit-options.cmake" "$WK" ) > "$B/configure.log" 2>&1 \
        || { tail -30 "$B/configure.log" >&2; echo "error: configure failed (see $B/configure.log)" >&2; exit 1; }
    # Three targets are the whole engine: the library our process loads, and the two helper
    # executables it starts (WPEWebProcess, WPENetworkProcess). There is no GPU process.
    ninja -C "$B" -j "$JOBS" WebKit WPEWebProcess WPENetworkProcess
    cmake --install "$B" > "$B/install.log" 2>&1 || { tail -20 "$B/install.log" >&2; exit 1; }
    echo "installed into $B/root; run Florence with WEBKIT_EXEC_PATH=$B/root/libexec/wpe-webkit-2.0 if it is not found"
    ;;
cross)
    . tools/common.sh
    DEPS="${FL_DEPS_ROOT:-$BUILD/depsroot}"    # glib, libsoup, ... for the target, in /usr/local layout
    [ -d "$DEPS/lib/pkgconfig" ] || { echo "error: no target dependency tree at $DEPS (set FL_DEPS_ROOT); the port does not build glib, libsoup, sqlite, harfbuzz ... yet: docs/webkit-port.md lists them" >&2; exit 1; }
    B="$BUILD/webkit-cross"; mkdir -p "$B"
    # The cross toolchain of tools/common.sh as a CMake toolchain file: Xcode 12's clang driven
    # directly, the iPhoneOS 14.4 SDK, no pkg-config or Homebrew from the host.
    cat > "$B/toolchain.cmake" <<TC
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR arm64)
set(CMAKE_C_COMPILER "$CLANG")
set(CMAKE_CXX_COMPILER "${CLANG}++")
set(CMAKE_OSX_SYSROOT "$SDK")
set(CMAKE_C_FLAGS_INIT "-target arm64-apple-ios14.4 -fno-builtin -fno-stack-protector -D_FORTIFY_SOURCE=0")
set(CMAKE_CXX_FLAGS_INIT "-target arm64-apple-ios14.4 -fno-builtin -fno-stack-protector -D_FORTIFY_SOURCE=0")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH "$DEPS")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
TC
    ( cd "$B" && env PKG_CONFIG_LIBDIR="$DEPS/lib/pkgconfig" PKG_CONFIG_PATH= cmake -GNinja -DPORT=WPE -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DCMAKE_TOOLCHAIN_FILE="$B/toolchain.cmake" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -C "$FL_DIR/config/webkit-options.cmake" "$WK" ) 2>&1 | tee "$B/configure.log" | tail -30
    echo "configure done; the cross build itself (ninja) has not been attempted: see docs/webkit-port.md" >&2
    ;;
*) echo "usage: $0 host|cross" >&2; exit 2 ;;
esac
