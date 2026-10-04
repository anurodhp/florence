#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# CC for autotools builds (scripts/build_autotools_lib.sh). libtool bakes CC in at configure time and silently drops
# -nostdlib and bare dylib paths from LDFLAGS, so a shared-library link (-dynamiclib) gets the explicit dylib list
# (owners first, libSystem.B last: the SDK's libSystem stub would win otherwise and fail the bind audit) appended
# here; everything else (compiles, configure's executable tests) passes through. FL_REAL_CC and FL_LINK_TAIL come
# from the caller.
for a in "$@"; do
    case "$a" in -dynamiclib|-shared) exec $FL_REAL_CC "$@" -nostdlib $FL_LINK_TAIL;; esac
done
exec $FL_REAL_CC "$@"
