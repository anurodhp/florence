#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# Shared toolchain and helpers for Florence's build scripts. SOURCED, never run.
#
# Same recipe as the iokit repo's tools/userland_staging/x11_common.sh (read its
# header for the reasons): Xcode 12's clang and iPhoneOS 14.4 SDK for compiling,
# the daily-driver Xcode's newer ld for linking (Xcode 12's ld64-609 crashes
# under -fixup_chains), -nostdlib against the iokit port's own libc_build/system
# dylibs with each symbol's real owner listed first, never the SDK's .tbd stubs.
set -euo pipefail

FL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
IOKIT_DIR="${IOKIT_DIR:-$FL_DIR/../iokit}"
IOKIT_DIR="$(cd "$IOKIT_DIR" 2>/dev/null && pwd -P)" || { echo "error: iokit repo not found (set IOKIT_DIR)" >&2; exit 1; }
IOKIT_LIBC="$IOKIT_DIR/tools/userland_staging/libc_build"
SYS="$IOKIT_LIBC/system"
[ -f "$SYS/libSystem.B.dylib" ] || { echo "error: $SYS/libSystem.B.dylib missing -- build the iokit userland first" >&2; exit 1; }

TP="$FL_DIR/third_party"
BUILD="$FL_DIR/build"
ROOT="$BUILD/root"                  # staged install tree, mirrors the target filesystem
PREFIX=/usr/local                   # target prefix
INC="$ROOT$PREFIX/include"
LIB="$ROOT$PREFIX/lib"
mkdir -p "$INC" "$LIB" "$ROOT$PREFIX/bin" "$ROOT$PREFIX/share"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
[ -x "$CLANG" ] && [ -d "$SDK" ] || { echo "error: Xcode-12.app not found at $XCODE12" >&2; exit 1; }
NEWLD_BINDIR="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin"
[ -x "$NEWLD_BINDIR/ld" ] || { echo "error: no usable ld under $(xcode-select -p)" >&2; exit 1; }

FL_OPT="${FL_OPT:--O2}"
# Hosted POSIX client flags: -fno-builtin and -D_FORTIFY_SOURCE=0 are load-bearing
# (x11_common.sh: the SDK's fortify inlines recurse; -fno-builtin keeps memcpy and
# friends calling libsystem_platform's arm64 routines).
FL_CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c
    -fno-builtin -fno-stack-protector $FL_OPT -fno-common
    -D_FORTIFY_SOURCE=0
    -Wno-nullability-completeness -Wno-deprecated-declarations
    -I "$INC")

# The libSystem tier, owners first (bind audit's rule), libSystem.B last.
FL_SYS_DYLIBS=("$SYS/libsystem_kernel.dylib" "$SYS/libsystem_c.dylib"
    "$SYS/libsystem_pthread.dylib" "$SYS/libsystem_malloc.dylib"
    "$SYS/libsystem_platform.dylib" "$SYS/libdyld.dylib"
    "$SYS/libSystem.B.dylib")
# Extra owners other libraries here need (libinfo: getaddrinfo, getpwnam;
# libresolv: res_*, inet_*; libremovefile: rename/fsync).
FL_NET_DYLIBS=("$SYS/libinfo.dylib" "$SYS/libresolv.dylib" "$SYS/libremovefile.dylib")

FL_OK=0
FL_FAIL=0
# fl_compile <objdir> <src.c> [clang args...]: object named from the path so two
# same-basename sources never collide. Failures are counted, not fatal, so one
# run lists every gap; fl_compile_report then fails the build.
fl_compile() {
    local objdir="$1" src="$2"; shift 2
    mkdir -p "$objdir"
    local base; base=$(echo "${src#$TP/}" | sed 's#\.[cm]$##; s#/#_#g')
    if "$CLANG" "${FL_CFLAGS[@]}" "$@" "$src" -o "$objdir/$base.o" 2>"$objdir/$base.err"; then
        FL_OK=$((FL_OK + 1)); [ -s "$objdir/$base.err" ] || rm -f "$objdir/$base.err"
    else
        FL_FAIL=$((FL_FAIL + 1))
        echo "  gap: ${src#$TP/} -- $(grep -m1 'error:' "$objdir/$base.err")" >&2
    fi
}
fl_compile_report() {
    echo "$1: $FL_OK compiled, $FL_FAIL failed"
    [ "$FL_FAIL" -eq 0 ] || { echo "error: $1 has compile failures (see the *.err files in its object dir)" >&2; exit 1; }
    FL_OK=0; FL_FAIL=0
}

# fl_dylib_versions M:m:r -> "major compat current", libtool darwin's arithmetic
# for -version-number M:m:r (as x11_common.sh).
fl_dylib_versions() {
    local M m r; IFS=: read -r M m r <<< "$1"
    local cur=$((M + m)); echo "$M $((cur + 1)) $((cur + 1)).$r"
}

# fl_link_dylib <stem> <M:m:r> <objdir> [libs / flags...]
#   writes $LIB/lib<stem>.<major>.dylib (install name /usr/local/lib/...), the
#   link-time symlink lib<stem>.dylib, then runs the bind audit.
fl_link_dylib() {
    local stem="$1" vn="$2" objdir="$3"; shift 3
    local major compat current; read -r major compat current <<< "$(fl_dylib_versions "$vn")"
    local out="$LIB/lib$stem.$major.dylib"
    rm -f "$out" "$LIB/lib$stem.dylib"
    if ! "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
        -install_name "$PREFIX/lib/lib$stem.$major.dylib" \
        -compatibility_version "$compat" -current_version "$current" \
        -Wl,-not_for_dyld_shared_cache -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
        -o "$out" "$objdir"/*.o "$@" "${FL_SYS_DYLIBS[@]}" 2>"$objdir/link.err"; then
        echo "error: lib$stem link failed; undefined symbols:" >&2
        grep '^  "' "$objdir/link.err" | sed 's/,.*//' | sort -u >&2
        tail -20 "$objdir/link.err" >&2
        exit 1
    fi
    ln -s "lib$stem.$major.dylib" "$LIB/lib$stem.dylib"
    echo "wrote $out"
    "$FL_DIR/tools/bind_audit.sh" "$out"
}

# fl_link_exe <out> <objdir> [libs / flags...]: dynamic executable, Csu start
# files, LC_MAIN / LC_LOAD_DYLINKER asserted, bind audit.
fl_link_exe() {
    local exe="$1" objdir="$2"; shift 2
    local csu="$IOKIT_DIR/third_party/Csu"
    mkdir -p "$(dirname "$exe")"
    rm -f "$exe"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
        -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
        -o "$exe" "$csu/start.s" "$csu/crt.c" "$objdir"/*.o "$@" "${FL_SYS_DYLIBS[@]}" 2>"$objdir/link.err" || {
        echo "error: $(basename "$exe") link failed; undefined symbols:" >&2
        grep '^  "' "$objdir/link.err" | sed 's/,.*//' | sort -u >&2
        tail -20 "$objdir/link.err" >&2; exit 1; }
    otool -l "$exe" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || { echo "error: $exe: no dyld" >&2; exit 1; }
    otool -l "$exe" | grep -q "cmd LC_MAIN" || { echo "error: $exe: no LC_MAIN" >&2; exit 1; }
    echo "wrote $exe"
    "$FL_DIR/tools/bind_audit.sh" "$exe"
}

# fl_stage_headers <dest-subdir under include> <files...>
fl_stage_headers() { local d="$INC/$1"; shift; mkdir -p "$d"; cp "$@" "$d/"; }
fl_require() { [ -e "$1" ] || { echo "error: $1 not found -- ${2:-run setup_third_party.sh}" >&2; exit 1; }; }
