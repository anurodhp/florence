#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
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
[ -d "$FL_DIR/build/root" ] || { echo "error: nothing built (build/root missing)" >&2; exit 1; }
cd "$FL_DIR/build/root"
paths=("$@"); [ "${#paths[@]}" -gt 0 ] || paths=(.)
tar cf - "${paths[@]}" | "${SSH[@]}" "$PI_USER@$PI_HOST" 'tar xmf - -C / && echo deployed'
