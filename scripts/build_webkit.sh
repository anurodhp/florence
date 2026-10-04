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
scripts/webkit_fixes.sh         # two missing #if guards the option set exposes; asserts its anchors

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
    # The Pi: clang 21 (C++23) against the iPhoneOS 14.4 SDK, the LLVM 20 libc++ and the dependency stack staged by
    # scripts/build_*.sh in build/root (see docs/HANDOFF.md for the order). Configure only until the stack is complete.
    . tools/common.sh
    . tools/cmake_cross.sh
    export FL_CMAKE_C_CLANG=daily
    B="$BUILD/webkit-cross"; mkdir -p "$B"
    fl_cmake_toolchain "$B/toolchain.cmake"
    # WebKit's CMake reads APPLE as "a Cocoa port" (WebKitLegacy, Xcode SDK tools, Mach-O file lists ...); this is the
    # toolkit-less port on a Darwin target, which is what its Linux branches describe. CMAKE_PROJECT_INCLUDE runs right
    # after project(): clear APPLE for WebKit's own logic (the compiler, linker and flags were already chosen as Darwin's)
    # and keep the GNU-ld-only --no-undefined from being added (WebKitCompilerFlags.cmake:437 skips it when this is set).
    cat >| "$B/after_project.cmake" <<'AP'
set(APPLE FALSE)
set(ENABLED_COMPILER_SANITIZERS "none")
AP
    ( cd "$B" && env PKG_CONFIG_LIBDIR="$FL_PKGCFG" PKG_CONFIG_PATH= cmake -GNinja -DPORT=WPE -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DCMAKE_TOOLCHAIN_FILE="$B/toolchain.cmake" -DCMAKE_OSX_SYSROOT="$SDK" -DCMAKE_PROJECT_INCLUDE="$B/after_project.cmake" -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -C "$FL_DIR/config/webkit-options.cmake" "$WK" ) > "$B/configure.log" 2>&1 \
        || { grep -B2 -A12 "CMake Error" "$B/configure.log" | head -60 >&2; echo "error: configure failed (see $B/configure.log)" >&2; exit 1; }
    echo "configure done ($B)"
    [ "${2:-}" = build ] && ninja -C "$B" -j "${JOBS:-$(sysctl -n hw.ncpu)}" WebKit WPEWebProcess WPENetworkProcess
    ;;
*) echo "usage: $0 host|cross" >&2; exit 2 ;;
esac
