#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# The few edits the dependency sources need on this target. Idempotent; each asserts its anchor, so a
# different upstream stops the build instead of building something else (same pattern as webkit_fixes.sh).
# Under the iokit port's rule these become commits in anurodhp/* forks.
set -euo pipefail
cd "$(dirname "$0")/.."
# edit <file> <anchor> <replacement> <why>
edit() {
    local f="$1" old="$2" new="$3"
    [ -f "$f" ] || { echo "error: $f missing" >&2; exit 1; }
    if grep -qF -- "$new" "$f"; then return 0; fi
    grep -qF -- "$old" "$f" || { echo "error: anchor not found in $f: $old" >&2; exit 1; }
    python3 - "$f" "$old" "$new" <<'PY'
import sys
f, old, new = sys.argv[1:4]
s = open(f).read(); assert old in s
open(f, 'w').write(s.replace(old, new, 1))
PY
    echo "edited $f"
}
# libepoxy 1.5.4 src/meson.build:58 passes "-compatibility_version 1" as ONE argument; ld64 rejects the space.
edit third_party/libepoxy/src/meson.build \
    "'-compatibility_version 1', '-current_version 1.0'," \
    "'-Wl,-compatibility_version,1', '-Wl,-current_version,1.0',"
