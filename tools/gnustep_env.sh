#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# GNUstep side of the cross build. SOURCED by scripts/build_florence.sh pi
# after tools/common.sh, with FL_GS_OWNERS already set -- never run.
#
# The iokit port (xnu-iokit-pi3) already knows how to compile and link against its
# GNUstep: tools/userland_staging/gnustep_common.sh (gs_setup_link, GS_BASE_SYSLIBS).
# That file is written to be sourced from its own directory and clobbers ROOT, SYS,
# PREFIX, CLANG..., so it is sourced in a subshell and only the results are taken:
#   FL_GS_LINK     -nostdlib -Wl,-Z, -L linkdir, -dylib_file maps for libSystem's re-exports
#   FL_GS_LIBFLAGS -l flags for every owner dylib given (owners first, libSystem tier after)
#   FL_GS_HEADERS  the installed GNUstep headers (Foundation/, AppKit/ ...)
#   FL_GS_LIBS     the installed GNUstep libraries (libgnustep-gui/base)
#   FL_GS_OBJC4    objc4's public headers (the runtime here is Apple's objc4, not libobjc2)
set -euo pipefail
FL_GS_TOOLS="$IOKIT_DIR/tools/userland_staging"
fl_require "$FL_GS_TOOLS/gnustep_common.sh" "is IOKIT_DIR the xnu-iokit-pi3 checkout?"
fl_require "$FL_GS_TOOLS/bind_target_audit.sh"
GSL="$IOKIT_LIBC/gnustep/root/usr/GNUstep/System/Library"
fl_require "$GSL/Libraries/libgnustep-gui.dylib" "run the iokit repo's build_gnustep_{host_tools,base,gui,back}.sh"
fl_require "$GSL/Libraries/libgnustep-base.dylib" "run the iokit repo's build_gnustep_base.sh"

# direct dependencies of the executable besides the libSystem tier, owners first: the caller lists them
# (the WebKit and GLib dylibs the glue calls), because which ones there are depends on the engine.
[ "${#FL_GS_OWNERS[@]}" -gt 0 ] || { echo "error: FL_GS_OWNERS (the engine's dylibs) must be set before sourcing tools/gnustep_env.sh" >&2; exit 1; }
eval "$(cd "$FL_GS_TOOLS" && bash -c '
    . ./gnustep_common.sh
    gs_setup_link "$@" "${GS_BASE_SYSLIBS[@]}"
    printf "FL_GS_LINK=%q\nFL_GS_LIBFLAGS=%q\nFL_GS_HEADERS=%q\nFL_GS_LIBS=%q\nFL_GS_OBJC4=%q\n" \
        "$LINK" "$LIBFLAGS" "$GS_SYS_HEADERS" "$GS_SYS_LIBS" "$OUT/objc4_public_headers"
' _ "${FL_GS_OWNERS[@]}")"

# Compiler flags. The Objective-C flags are asked of the port's own installed gnustep-config,
# exactly as build_foundation_smoketest.sh does (runtime defines, -fconstant-string-class=
# NSConstantString so @"..." is an NSConstantString, exceptions, blocks); never hand-written.
GSDIR="$IOKIT_LIBC/gnustep"
fl_require "$GSDIR/GNUstep-build.conf" "run the iokit repo's build_gnustep_*.sh (an app build writes it)"
FL_GS_OBJCFLAGS="$(GNUSTEP_CONFIG_FILE="$GSDIR/GNUstep-build.conf" GNUSTEP_MAKEFILES="$GSDIR/root/usr/GNUstep/System/Library/Makefiles" \
    "$GSDIR/root/usr/GNUstep/System/Tools/gnustep-config" --objc-flags | sed 's/-MMD -MP //')"
[ -n "$FL_GS_OBJCFLAGS" ] || { echo "error: gnustep-config --objc-flags returned nothing" >&2; exit 1; }
FL_GS_OBJCFLAGS="$FL_GS_OBJCFLAGS -D_FORTIFY_SOURCE=0 -I$FL_GS_HEADERS -I$FL_GS_OBJC4"

# fl_link_gs_exe <out> <objdir> [libs...]: the recipe the port uses for its GNUstep apps
# (gs_build_app): -nostdlib against owner dylibs, no Csu start files (ld gives LC_MAIN),
# then the port's own bind audit, which is not optional (a clean link can still abort in dyld).
fl_link_gs_exe() {
    local exe="$1" objdir="$2"; shift 2
    mkdir -p "$(dirname "$exe")"; rm -f "$exe"
    # shellcheck disable=SC2086
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 $FL_GS_LINK "$objdir"/*.o "$@" \
        $FL_GS_LIBFLAGS -L"$FL_GS_LIBS" -lgnustep-gui -lgnustep-base -o "$exe" 2>"$objdir/link.err" || {
        echo "error: $(basename "$exe") link failed; undefined symbols:" >&2
        grep '^  "' "$objdir/link.err" | sed 's/,.*//' | sort -u >&2
        tail -20 "$objdir/link.err" >&2; exit 1; }
    otool -l "$exe" | grep -q "cmd LC_MAIN" || { echo "error: $exe: no LC_MAIN" >&2; exit 1; }
    (cd "$FL_GS_TOOLS" && ./bind_target_audit.sh "$exe") || { echo "error: $exe fails bind_target_audit" >&2; exit 1; }
    echo "wrote $exe"
}
