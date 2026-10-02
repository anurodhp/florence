#!/bin/bash
# Builds the on-target smoke tests into build/root/usr/local/bin.
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
fl_fail_if_missing() { [ -e "$1" ] || { echo "error: $1 missing -- $2" >&2; exit 1; }; }
fl_fail_if_missing "$LIB/libcurl.dylib" "run scripts/build_curl.sh"
OBJ="$BUILD/obj/tests"; rm -rf "$OBJ"; mkdir -p "$OBJ"
fl_compile "$OBJ" "$PWD/tests/curl_get.c"
fl_compile_report tests
fl_link_exe "$ROOT$PREFIX/bin/curl_get" "$OBJ" "$LIB/libcurl.dylib"
