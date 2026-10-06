#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# libcrashhook.1.dylib: preloadable crash report (tests/pi/crashhook.c + frontend/flo_diag.c) for WebKit's helper processes.
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
O="$BUILD/obj/crashhook"; rm -rf "$O"; mkdir -p "$O"
fl_compile "$O" "$FL_DIR/tests/pi/crashhook.c"
fl_compile "$O" "$FL_DIR/frontend/flo_diag.c" -I "$FL_DIR/frontend"
fl_compile_report crashhook
fl_link_dylib crashhook 1:0:0 "$O"
