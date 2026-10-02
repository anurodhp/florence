#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# Builds nsgenbind for the BUILD HOST (not cross): it turns NetSurf's WebIDL into the C bindings
# between Duktape and the DOM at build time and never runs on the Pi. Only needed for
# FLO_JS=1 scripts/build_netsurf.sh gnustep (optional JavaScript).
#   Needs flex and bison >= 3. macOS ships bison 2.3: `brew install bison flex` (found in the
#   usual Homebrew prefixes below, or put a newer bison first in PATH).
#   Needs the NetSurf buildsystem installed by scripts/build_netsurf_libs.sh.
# Output: build/hosttools/bin/nsgenbind
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD="$PWD/build"; TP="$PWD/third_party"; NSROOT="$BUILD/nsroot"; HT="$BUILD/hosttools"
[ -f "$TP/nsgenbind/Makefile" ] || { echo "error: third_party/nsgenbind missing -- run ./setup_third_party.sh" >&2; exit 1; }
[ -d "$NSROOT/share/netsurf-buildsystem" ] || { echo "error: $NSROOT/share/netsurf-buildsystem missing -- run scripts/build_netsurf_libs.sh first" >&2; exit 1; }

# a bison >= 3
BISON=""
for b in "$(command -v bison || true)" /opt/homebrew/opt/bison/bin/bison /usr/local/opt/bison/bin/bison; do
    [ -x "$b" ] || continue
    v=$("$b" --version | head -1 | sed 's/[^0-9.]*\([0-9][0-9]*\).*/\1/')
    [ "${v:-0}" -ge 3 ] && { BISON="$b"; break; }
done
[ -n "$BISON" ] || { echo "error: bison >= 3 not found (macOS's is 2.3): brew install bison" >&2; exit 1; }
command -v flex >/dev/null || [ -x /opt/homebrew/opt/flex/bin/flex ] || [ -x /usr/local/opt/flex/bin/flex ] || { echo "error: flex not found: brew install flex" >&2; exit 1; }
echo "using $BISON"

rm -rf "$TP"/nsgenbind/build-*
mkdir -p "$HT"
# a clean host environment: nothing from the cross toolchain may leak in
( cd "$TP/nsgenbind" && env -i HOME="$HOME" CC=cc \
    PATH="$(dirname "$BISON"):/opt/homebrew/opt/flex/bin:/usr/local/opt/flex/bin:/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:/usr/local/bin" \
    make -j4 PREFIX="$HT" NSSHARED="$NSROOT/share/netsurf-buildsystem" install ) > "$BUILD/nsgenbind.log" 2>&1 || {
    tail -30 "$BUILD/nsgenbind.log" >&2; echo "error: nsgenbind build failed (log: $BUILD/nsgenbind.log)" >&2; exit 1; }
[ -x "$HT/bin/nsgenbind" ] || { echo "error: $HT/bin/nsgenbind not produced" >&2; exit 1; }
echo "wrote $HT/bin/nsgenbind"
