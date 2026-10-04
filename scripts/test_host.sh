#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# Builds and runs tests/smoke_engine.c: the engine glue (flo_engine.c, flo_platform.c) end to end with no UI,
# against the WebKit that scripts/build_webkit.sh host installed. Needs no X server and no GNUstep: it loads
# tests/pages/first.html, checks the first frame's pixels and the title/address events, clicks a link, and
# scrolls with the wheel. Exit status 0 only if everything held.
set -euo pipefail
cd "$(dirname "$0")/.."
FL_DIR="$PWD"
B="$FL_DIR/build/florence-host"; mkdir -p "$B"
WK="${WEBKIT_ROOT:-$FL_DIR/build/webkit-host/root}"
[ -f "$WK/lib/pkgconfig/wpe-webkit-2.0.pc" ] || { echo "error: no WPE WebKit in $WK -- run scripts/build_webkit.sh host" >&2; exit 1; }
export PKG_CONFIG_PATH="$WK/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
PC="wpe-webkit-2.0 wpe-platform-2.0 glib-2.0"
# shellcheck disable=SC2046
gcc -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter $(pkg-config --cflags $PC) -Ifrontend \
    tests/smoke_engine.c frontend/flo_engine.c frontend/flo_platform.c -o "$B/smoke_engine" \
    $(pkg-config --libs $PC) -Wl,-rpath,"$WK/lib"
DATA="$(mktemp -d)"; trap 'rm -rf "$DATA"' EXIT
# the helper processes (WPEWebProcess, WPENetworkProcess) are found through the install prefix, or here
# the install strips the run path of the helper processes: they find libWPEWebKit through this
export LD_LIBRARY_PATH="$WK/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export WEBKIT_EXEC_PATH="${WEBKIT_EXEC_PATH:-$WK/libexec/wpe-webkit-2.0}"
exec timeout 300 "$B/smoke_engine" "$FL_DIR/tests/pages/first.html" "$DATA"
