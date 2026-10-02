#!/bin/bash
# Builds IJG libjpeg 9f (third_party/jpeg, jpegsrc.v9f) as /usr/local/lib/libjpeg.9.dylib.
# Sources: Makefile.in's LIBSOURCES with jmemnobs.c as the memory manager
# (configure's default MEMORYMGR, no temp-file backing store: fine for a browser,
# jpeg decode here is whole-image in RAM). jconfig.h is upstream's jconfig.txt
# (the generic, no-configure configuration it ships for exactly this case).
# NetSurf only decodes (jpeg_*decompress), through the standard API.
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
SRC="$TP/jpeg"
fl_require "$SRC/jpeglib.h"
OBJ="$BUILD/obj/jpeg"; rm -rf "$OBJ"; mkdir -p "$OBJ/cfg"
cp "$SRC/jconfig.txt" "$OBJ/cfg/jconfig.h"
FILES=$(awk '/^LIBSOURCES/ {on=1} on {cont = ($0 ~ /\\$/); gsub(/\\/,""); print} on && !cont {exit}' "$SRC/Makefile.in" \
        | sed 's/^LIBSOURCES *=//; s/@MEMORYMGR@\.c/jmemnobs.c/' | tr -s ' \t\n' ' ')
fl_stage_headers "" "$SRC/jpeglib.h" "$SRC/jerror.h" "$SRC/jmorecfg.h" "$OBJ/cfg/jconfig.h"
for f in $FILES; do fl_compile "$OBJ" "$SRC/$f" -I "$OBJ/cfg" -I "$SRC"; done
fl_compile_report libjpeg
# JPEG_LIB_VERSION 14:0:5 upstream (Makefile.in) -> -version-number 9:5:0
fl_link_dylib jpeg 9:5:0 "$OBJ"
