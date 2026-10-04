#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# libflocompat.dylib: the libSystem exports the iokit port lacks (compat/*.c), plus the libm functions its
# libsystem_m does not export (about 80: expf, logf, log2, fmax, copysign, nearbyint, lrint ...), compiled from the
# same unmodified FreeBSD msun 13.2 sources and with the same recipe as the iokit port's own build_libm_dylib.sh
# (-include libm_msun_compat.h, msun's math.h left out so Darwin's is used). Long double variants are not built.
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
O="$BUILD/obj/compat"; rm -rf "$O"; mkdir -p "$O"
for f in "$FL_DIR"/compat/*.c; do fl_compile "$O" "$f"; done
MSUN="$IOKIT_DIR/third_party/freebsd-src/lib/msun/src"; fl_require "$MSUN/e_expf.c" "run the iokit port's setup_third_party.sh (freebsd-src)"
MC="$BUILD/obj/msun_src"; rm -rf "$MC"; mkdir -p "$MC"
cp "$MSUN"/*.c "$MSUN"/*.h "$MC/"; rm -f "$MC/math.h"
COMPAT_H="$IOKIT_DIR/tools/userland_staging/libm_msun_compat.h"
for f in e_expf e_logf e_log2 e_log2f e_log10f s_exp2f s_expm1f s_log1pf s_tanf e_asinf e_acosf \
         s_cbrtf e_hypotf s_roundf s_truncf s_frexpf s_modff s_copysign s_lrint e_rem_pio2f k_rem_pio2; do
    fl_compile "$O" "$MC/$f.c" -I "$MC" -include "$COMPAT_H" -Wno-everything
done
fl_compile_report compat
fl_link_dylib flocompat 1:0:0 "$O"
