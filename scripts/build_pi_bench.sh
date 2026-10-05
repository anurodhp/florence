#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# Builds tests/pi/draw_bench.m (how fast GNUstep draws a frame, in several ways) for the Pi into build/root/usr/local/bin.
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
. tools/meson_cross.sh
FL_GS_OWNERS=("$LIB/libflocompat.dylib")
. tools/gnustep_env.sh
OBJ="$BUILD/obj/draw_bench"; rm -rf "$OBJ"; mkdir -p "$OBJ"
# shellcheck disable=SC2086
for f in "$FL_DIR/tests/pi/draw_bench.m" "$FL_DIR/frontend/FloCairo.m"; do
    fl_compile "$OBJ" "$f" -I"$FL_DIR/frontend" $FL_GS_OBJCFLAGS $FL_OPT
done
fl_compile_report draw_bench
fl_link_gs_exe "$ROOT$PREFIX/bin/draw_bench" "$OBJ"
