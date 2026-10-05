#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# Helper for the build_*.sh scripts of Meson projects. SOURCED after tools/common.sh, never run.
#
#   fl_meson_cross            writes $BUILD/meson-cross.ini: Xcode 12's clang driven directly (the
#                             recipe of tools/common.sh), -nostdlib against the iokit port's dylibs,
#                             a pkg-config that only sees $LIB/pkgconfig (the build host's never leaks in)
#   fl_pc <name> <ver> <libs> [cflags] [requires]
#                             writes $LIB/pkgconfig/<name>.pc for a library the iokit port provides
#   fl_meson_build <srcdir> <builddir> [meson options...]
#                             setup + compile + install into $ROOT (DESTDIR), stripping nothing
FL_PKGCFG="$LIB/pkgconfig"; mkdir -p "$FL_PKGCFG"
FL_MESON_CROSS="$BUILD/meson-cross.ini"
fl_meson_cross() {
    local cflags="-isysroot $SDK -target arm64-apple-ios14.4 -fno-builtin -fno-stack-protector -D_FORTIFY_SOURCE=0 $FL_OPT -fno-common -Wno-nullability-completeness -Wno-deprecated-declarations -Wno-error -D__API_AVAILABLE_PLATFORM_iosmac(x)=macCatalyst,introduced=x -D__API_DEPRECATED_PLATFORM_iosmac(x,y)=macCatalyst,introduced=x,deprecated=y -I$INC"
    local lflags="-isysroot $SDK -target arm64-apple-ios14.4 -nostdlib -B$NEWLD_BINDIR -Wl,-fixup_chains -L$LIB -lflocompat"
    local a l
    a=$(for f in $cflags; do printf "'%s', " "$f"; done); a="${a%, }"
    l=$(for f in $lflags; do printf "'%s', " "$f"; done)
    # owners first, libSystem.B last (the bind audit's rule); libflocompat (-l above) covers the rest
    local sysl=("${FL_SYS_DYLIBS[@]:0:6}" "${FL_NET_DYLIBS[@]}" "$SYS/libsystem_notify.dylib" "$SYS/libsystem_m.dylib" "${FL_SYS_DYLIBS[6]}")
    l="$l$(for f in "${sysl[@]}"; do printf "'%s', " "$f"; done)"; l="${l%, }"
    # C++: the iokit port's LLVM 11 libc++ (headers from its pinned llvm-project, the system libc++.dylib)
    local lcxx="$(for f in $lflags; do printf "'%s', " "$f"; done)'$SYS/libc++.dylib', $(for f in "${sysl[@]}"; do printf "'%s', " "$f"; done)"; lcxx="${lcxx%, }"
    cat >| "$FL_MESON_CROSS" <<EOT
[binaries]
c = ['$CLANG']
cpp = ['$CLANG++']
objc = ['$CLANG']
ar = '$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ar'
strip = '$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/strip'
ranlib = '$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ranlib'
pkg-config = '/opt/homebrew/bin/pkg-config'

[built-in options]
c_args = [$a]
cpp_args = [$a, '-nostdinc++', '-isystem', '$IOKIT_DIR/third_party/llvm-project/libcxx/include', '-D_LIBCPP_DISABLE_AVAILABILITY']
c_link_args = [$l]
cpp_link_args = [$lcxx]
default_library = 'shared'

[properties]
needs_exe_wrapper = true

[host_machine]
system = 'darwin'
subsystem = 'ios'
cpu_family = 'aarch64'
cpu = 'arm64'
endian = 'little'
EOT
}
# fl_pc name version "-lfoo" ["-I... cflags"] ["Requires"]
fl_pc() {
    cat > "$FL_PKGCFG/$1.pc" <<EOT
prefix=$PREFIX
libdir=$LIB
includedir=$INC
Name: $1
Description: $1
Version: $2
Libs: -L\${libdir} $3
Cflags: -I\${includedir} ${4:-}
${5:+Requires: $5}
EOT
}
fl_meson_build() {
    local src="$1" bld="$2"; shift 2
    fl_meson_cross
    # pkg-config reports $ROOT paths (headers and libs are staged there, installed under $PREFIX on the Pi)
    ( cd "$src" && env PYTHONPATH="$FL_DIR/tools/pyshim" PKG_CONFIG_LIBDIR="$FL_PKGCFG" PKG_CONFIG_PATH= PKG_CONFIG_SYSROOT_DIR= \
        meson setup "$bld" --cross-file "$FL_MESON_CROSS" --prefix "$PREFIX" --libdir lib \
        --buildtype plain "$@" ) || return 1
    export PYTHONPATH="$FL_DIR/tools/pyshim"
    # FL_NINJA_KEEP_GOING=1: keep going past targets that cannot link here (test programs); the install
    # step below then fails if anything it installs is missing
    ninja -C "$src/$bld" ${FL_NINJA_KEEP_GOING:+-k 0} || [ -n "${FL_NINJA_KEEP_GOING:-}" ] || return 1
    DESTDIR="$ROOT" meson install -C "$src/$bld" --no-rebuild >/dev/null
    # the installed .pc files name /usr/local (the target); the build host finds the staged tree
    sed -i '' "s#^prefix=$PREFIX\$#prefix=$ROOT$PREFIX#" "$FL_PKGCFG"/*.pc
}
# Meson installs Darwin dylibs with @rpath install names; the target has no rpath convention here, every
# library is referenced by its absolute install path (/usr/local/lib/...), as the other builds do.
fl_fix_install_names() {
    local f dep
    for f in "$@"; do
        [ -L "$f" ] && continue
        install_name_tool -id "$PREFIX/lib/$(basename "$f")" "$f"
        for dep in $(otool -L "$f" | awk '/@rpath|@loader_path/ {print $1}'); do
            install_name_tool -change "$dep" "$PREFIX/lib/$(basename "$dep")" "$f"
        done
        install_name_tool -delete_rpath "@loader_path/../subprojects/proxy-libintl" "$f" 2>/dev/null || true
    done
}
