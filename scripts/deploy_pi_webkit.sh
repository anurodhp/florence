#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# Copies what the engine needs to the Pi: the dylibs of build/root/usr/local/lib, the WebKit helper processes and
# data, and the test programs, symbol-stripped (strip -x keeps the exports and the fixup chains; libWPEWebKit goes
# from 105 MB to 63 MB) into build/deploy, then tools/deploy_to_pi.sh streams that tree over ssh.
#   scripts/deploy_pi_webkit.sh            # everything
#   scripts/deploy_pi_webkit.sh bin        # only usr/local/bin (test programs) -- the quick loop
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh
SB="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin"
D="$BUILD/deploy"; mkdir -p "$D$PREFIX"
what="${1:-all}"
sync_dir() {   # sync_dir <subdir under usr/local> [strip]
    local sub="$1" strip="${2:-}"
    mkdir -p "$D$PREFIX/$sub"
    rsync -a --delete --exclude='*.a' --exclude='*.la' --exclude='pkgconfig' --exclude='include' "$ROOT$PREFIX/$sub/" "$D$PREFIX/$sub/"
    if [ -n "$strip" ]; then
        find "$D$PREFIX/$sub" -type f \( -name '*.dylib' -o -perm -u+x \) ! -name '*.sh' ! -name '*.py' | while read -r f; do
            file "$f" | grep -q "Mach-O" && "$SB/strip" -x "$f" 2>/dev/null || true
        done
    fi
}
case "$what" in
bin) sync_dir bin ;;
all) sync_dir lib strip; sync_dir libexec strip; sync_dir bin; mkdir -p "$D$PREFIX/share"; rsync -a "$ROOT$PREFIX/share/wpe-webkit-2.0" "$D$PREFIX/share/" ;;
esac
mkdir -p "$D$PREFIX/share/florence" && rsync -a tests/pages "$D$PREFIX/share/florence/"
DEPLOY_ROOT="$D" tools/deploy_to_pi.sh $([ "$what" = bin ] && echo usr/local/bin usr/local/share/florence || echo usr/local/.)
