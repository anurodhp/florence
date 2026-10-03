#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# Proves that every option named in config/webkit-options.cmake is still defined by the pinned
# WebKit tree. CMake does not complain about a -D for an option that no longer exists: it just
# builds with the default, silently turning a "feature off" back on. Run by build_webkit.sh;
# runs anywhere (host tool: grep only, no toolchain, no iokit repo).
set -euo pipefail
FL_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
WK="${WEBKIT_DIR:-$FL_DIR/third_party/webkit}"
OPTS="$FL_DIR/config/webkit-options.cmake"
[ -f "$WK/Source/cmake/WebKitFeatures.cmake" ] || { echo "error: $WK is not a WebKit checkout -- run setup_third_party.sh" >&2; exit 1; }
bad=0; n=0
for name in $(sed -n 's/^flo_set(\([A-Z0-9_]*\) .*/\1/p' "$OPTS"); do
    n=$((n + 1))
    # defined by WEBKIT_OPTION_DEFINE (shared, or per port) or by a plain option()/set(... CACHE)
    if ! grep -rqE "(WEBKIT_OPTION_DEFINE|WEBKIT_OPTION_DEFAULT_PORT_VALUE|option)\(${name}[ )]" "$WK/Source/cmake" 2>/dev/null; then
        echo "error: $name is not an option of this WebKit (Source/cmake)" >&2; bad=$((bad + 1))
    fi
done
# every option must be set once only: a later duplicate would quietly win
dups="$(sed -n 's/^flo_set(\([A-Z0-9_]*\) .*/\1/p' "$OPTS" | sort | uniq -d)"
[ -z "$dups" ] || { echo "error: set twice in $OPTS: $dups" >&2; bad=$((bad + 1)); }
[ "$bad" -eq 0 ] || exit 1
echo "webkit options: $n checked against $(git -C "$WK" describe --tags 2>/dev/null || echo "$WK"): all defined"
