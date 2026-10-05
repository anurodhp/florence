#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# Copies build/root to the Pi's / over ssh (tar stream, nothing else is touched).
#   PI_HOST (default 10.0.0.142), PI_USER (root); auth: PI_PASS (default "darwin", the test
#   image's root password -- a test LAN only), or PI_PASS= (empty) to use an ssh key instead.
#   tools/deploy_to_pi.sh [subpath ...]   # e.g. usr/local/lib (default: everything)
set -euo pipefail
FL_DIR="$(cd "$(dirname "$0")/.." && pwd -P)"
PI_HOST="${PI_HOST:-10.0.0.142}"; PI_USER="${PI_USER:-root}"; PI_PASS="${PI_PASS-darwin}"
SSH=(ssh -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new)
if [ -n "${PI_PASS:-}" ]; then
    T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
    printf '#!/bin/sh\nprintf "%%s\\n" "$FL_PASS"\n' > "$T/askpass"; chmod 700 "$T/askpass"
    export FL_PASS="$PI_PASS" SSH_ASKPASS="$T/askpass" SSH_ASKPASS_REQUIRE=force DISPLAY="${DISPLAY:-:0}"
    SSH+=(-o PreferredAuthentications=password,keyboard-interactive -o NumberOfPasswordPrompts=1)
else
    SSH+=(-o BatchMode=yes)
fi
DEPLOY_ROOT="${DEPLOY_ROOT:-$FL_DIR/build/root}"   # scripts/deploy_pi_webkit.sh points this at a stripped copy
[ -d "$DEPLOY_ROOT" ] || { echo "error: nothing built ($DEPLOY_ROOT missing)" >&2; exit 1; }
cd "$DEPLOY_ROOT"
paths=("$@"); [ "${#paths[@]}" -gt 0 ] || paths=(.)
tar cf - "${paths[@]}" | "${SSH[@]}" "$PI_USER@$PI_HOST" 'tar xmf - -C / && echo deployed'
# A dropped connection can leave a cut-off file behind (tar then reports "Write error" or nothing at all): a truncated dylib maps
# and crashes with a bus error. Compare the sizes of every file over 1 MB with what the Pi has.
bad=0
while read -r sz f; do
    [ -n "$f" ] || continue
    remote=$("${SSH[@]}" "$PI_USER@$PI_HOST" "ls -l '/$f' | awk '{print \$5}'" 2>/dev/null || echo missing)
    [ "$remote" = "$sz" ] || { echo "error: /$f is $remote bytes on the Pi, expected $sz" >&2; bad=1; }
done < <(find "${paths[@]}" -type f -size +1M -exec stat -f '%z %N' {} + 2>/dev/null)
[ "$bad" = 0 ] || { echo "error: the deploy is incomplete, run it again" >&2; exit 1; }
echo "verified"

