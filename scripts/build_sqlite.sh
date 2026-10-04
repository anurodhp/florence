#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# SQLite 3.44.0 amalgamation as libsqlite3.dylib (WebKit: databases, HSTS/cookie stores via libsoup).
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
. tools/meson_cross.sh
SRC="$TP/sqlite"; fl_require "$SRC/sqlite3.c"
O="$BUILD/obj/sqlite"; rm -rf "$O"; mkdir -p "$O"
# no extension loading (no dlopen of arbitrary code), no threads beyond what pthread gives, small defaults
fl_compile "$O" "$SRC/sqlite3.c" -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION=1 -DSQLITE_ENABLE_FTS3 \
    -DSQLITE_ENABLE_FTS3_PARENTHESIS -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_USE_URI=1 -DSQLITE_WITHOUT_ZONEMALLOC -DSQLITE_ENABLE_LOCKING_STYLE=0 -Wno-everything
fl_compile_report sqlite
fl_link_dylib sqlite3 8:6:8 "$O" "${FL_NET_DYLIBS[@]}"
fl_stage_headers . "$SRC/sqlite3.h" "$SRC/sqlite3ext.h"
fl_pc sqlite3 3.44.0 "-L$LIB -lsqlite3"
