#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# The freedesktop shared-mime-info database (build/root/usr/local/share/mime/mime.cache and the XML) for the Pi. GIO decides
# the content type of a file:// URL from it; without it every file is application/octet-stream and WebKit shows nothing.
# update-mime-database is a build-host program, so this builds shared-mime-info natively (host glib and libxml2) into a scratch
# prefix and ships only the generated data (architecture independent).
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
SRC="$TP/shared-mime-info"; fl_require "$SRC/meson.build"
H="$BUILD/mime-host"; B="$BUILD/mime-build"
meson setup "$B" "$SRC" --wipe --prefix "$H" -Dupdate-mimedb=true -Dbuild-tools=true -Dbuild-translations=false -Dbuild-tests=false > "$BUILD/mime-configure.log" 2>&1 \
    || meson setup "$B" "$SRC" --prefix "$H" -Dupdate-mimedb=true -Dbuild-tools=true -Dbuild-translations=false -Dbuild-tests=false > "$BUILD/mime-configure.log" 2>&1 \
    || { tail -15 "$BUILD/mime-configure.log" >&2; exit 1; }
ninja -C "$B" install > "$BUILD/mime-build.log" 2>&1 || { tail -15 "$BUILD/mime-build.log" >&2; exit 1; }
mkdir -p "$ROOT$PREFIX/share"
rsync -a --delete "$H/share/mime/" "$ROOT$PREFIX/share/mime/"
echo "mime database: $(du -sh "$ROOT$PREFIX/share/mime" | cut -f1), $(ls "$ROOT$PREFIX/share/mime" | tr '\n' ' ')"
