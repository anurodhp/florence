#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# Builds Florence WITH JavaScript (NetSurf's bundled Duktape engine plus the DOM bindings that the
# host tool nsgenbind generates), end to end, in the right order:
#
#   1. fetch the pinned nsgenbind source        (./setup_third_party.sh, only if it is missing)
#   2. check what the tool build needs          (bison >= 3 and flex: macOS ships bison 2.3)
#   3. build NetSurf's libraries                (only if the buildsystem is not installed yet)
#   4. build nsgenbind for THIS Mac             (scripts/build_nsgenbind.sh, skipped if built)
#   5. cross-build the browser with JavaScript  (FLO_JS=1 scripts/build_netsurf.sh gnustep)
#   6. check the result really contains Duktape
#   7. optionally copy it to the Pi             (--deploy)
#
# Usage:  scripts/enable_js.sh [--check] [--rebuild-tools] [--deploy]
#   --check          only report whether the prerequisites are in place; build nothing
#   --rebuild-tools  rebuild nsgenbind even if it already exists
#   --deploy         run tools/deploy_to_pi.sh afterwards (PI_HOST/PI_USER/PI_PASS as there)
# JavaScript is still OFF when the browser starts: turn it on with View > Enable JavaScript
# (remembered), or start it with FLORENCE_JS=1. To go back to a build without it, simply run
# scripts/build_netsurf.sh gnustep (no FLO_JS).
set -euo pipefail
cd "$(dirname "$0")/.."

CHECK=0; REBUILD=0; DEPLOY=0
for a in "$@"; do
    case "$a" in
        --check) CHECK=1 ;;
        --rebuild-tools) REBUILD=1 ;;
        --deploy) DEPLOY=1 ;;
        -h|--help) awk 'NR > 2 && /^#/ { sub(/^# ?/, ""); print; next } NR > 2 { exit }' "$0"; exit 0 ;;
        *) echo "error: unknown option $a (try --help)" >&2; exit 2 ;;
    esac
done

BUILD="$PWD/build"; TP="$PWD/third_party"; NSROOT="$BUILD/nsroot"; NSGENBIND="$BUILD/hosttools/bin/nsgenbind"
APP_BIN="$BUILD/root/usr/local/bin/nsgnustep"
step() { printf '\n==> %s\n' "$*"; }
problems=0
need() { echo "  missing: $*" >&2; problems=$((problems + 1)); }

# a bison >= 3, wherever Homebrew put it
find_bison() {
    local b v
    for b in "$(command -v bison || true)" /opt/homebrew/opt/bison/bin/bison /usr/local/opt/bison/bin/bison; do
        [ -x "$b" ] || continue
        v=$("$b" --version | head -1 | sed 's/[^0-9.]*\([0-9][0-9]*\).*/\1/')
        if [ "${v:-0}" -ge 3 ]; then echo "$b"; return 0; fi
    done
    return 1
}
have_flex() { command -v flex >/dev/null || [ -x /opt/homebrew/opt/flex/bin/flex ] || [ -x /usr/local/opt/flex/bin/flex ]; }

step "1/7 nsgenbind source"
if [ -f "$TP/nsgenbind/Makefile" ]; then
    echo "  have third_party/nsgenbind"
elif [ "$CHECK" = 1 ]; then
    need "third_party/nsgenbind (run ./setup_third_party.sh)"
else
    ./setup_third_party.sh
fi

step "2/7 build tools for nsgenbind"
if [ -x "$NSGENBIND" ] && [ "$REBUILD" = 0 ]; then
    echo "  nsgenbind already built ($NSGENBIND): not needed"
else
    if b=$(find_bison); then echo "  bison: $b"; else need "bison >= 3 (macOS's is 2.3): brew install bison"; fi
    if have_flex; then echo "  flex: found"; else need "flex: brew install flex"; fi
fi

step "3/7 NetSurf libraries and the usual dependencies"
for f in "$NSROOT/lib/libcss.a" "$NSROOT/share/netsurf-buildsystem/makefiles/Makefile.tools"; do
    [ -e "$f" ] || { if [ "$CHECK" = 1 ]; then need "${f#$PWD/} (run scripts/build_netsurf_libs.sh)"; fi; }
done
if [ "$CHECK" = 0 ] && [ ! -e "$NSROOT/lib/libcss.a" ]; then
    echo "  NetSurf libraries missing: building them first"
    scripts/build_netsurf_libs.sh
fi
for f in "$BUILD/root/usr/local/lib/libcurl.dylib" "$BUILD/root/usr/local/lib/libjpeg.dylib"; do
    [ -e "$f" ] || need "${f#$PWD/} (run scripts/build_mbedtls.sh, build_curl.sh, build_jpeg.sh first)"
done

if [ "$CHECK" = 1 ]; then
    echo
    if [ "$problems" -eq 0 ]; then echo "all prerequisites are in place: scripts/enable_js.sh will build"; exit 0; fi
    echo "$problems prerequisite(s) missing (listed above)" >&2; exit 1
fi
[ "$problems" -eq 0 ] || { echo "error: $problems prerequisite(s) missing (listed above); nothing built" >&2; exit 1; }

step "4/7 nsgenbind (a tool for this Mac, not for the Pi)"
if [ -x "$NSGENBIND" ] && [ "$REBUILD" = 0 ]; then
    echo "  skipped: $NSGENBIND exists (--rebuild-tools to redo)"
else
    scripts/build_nsgenbind.sh
fi

step "5/7 building the browser with JavaScript (the first time takes a while: Duktape plus ~240 binding files)"
FLO_JS=1 scripts/build_netsurf.sh gnustep

step "6/7 checking the result"
if [ ! -x "$APP_BIN" ]; then echo "error: $APP_BIN was not produced" >&2; exit 1; fi
duk=$(nm "$APP_BIN" 2>/dev/null | grep -c ' _duk_' || true)
if [ "${duk:-0}" -ge 20 ]; then
    echo "  OK: the browser contains the Duktape engine ($duk duk_ symbols)"
else
    echo "error: build finished but the binary has no Duktape: it was not built with JavaScript" >&2; exit 1
fi

step "7/7 deploy"
if [ "$DEPLOY" = 1 ]; then
    tools/deploy_to_pi.sh
else
    echo "  not requested (run tools/deploy_to_pi.sh, or rerun with --deploy)"
fi

cat <<'MSG'

Done. JavaScript is built in but OFF until you turn it on:
  in the browser:   View > Enable JavaScript   (remembered in ~/.netsurf/Choices)
  or from a shell:  FLORENCE_JS=1 /Applications/Florence.app/Florence
It is slow on a Pi 3, and sites that need a modern engine will still not work.
MSG
