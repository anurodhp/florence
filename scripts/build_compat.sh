#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# libflocompat.dylib: the few libSystem exports the iokit port lacks (compat/flo_compat.c).
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
O="$BUILD/obj/compat"; rm -rf "$O"; mkdir -p "$O"
for f in "$FL_DIR"/compat/*.c; do fl_compile "$O" "$f"; done
fl_compile_report compat
fl_link_dylib flocompat 1:0:0 "$O"
