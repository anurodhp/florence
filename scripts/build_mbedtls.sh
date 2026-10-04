#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# Builds mbedTLS 3.6.x (third_party/mbedtls, v3.6.7) as the three upstream
# libraries, real dylibs under /usr/local/lib:
#   libmbedcrypto.16.dylib   libmbedx509.7.dylib   libmbedtls.21.dylib
# (sonames from library/CMakeLists.txt's SOVERSION lines). Small, pure C, no
# OpenSSL: the TLS stack libcurl links for https.
#
# Config: the stock include/mbedtls/mbedtls_config.h with the Arm crypto
# extension path turned off. The Pi 3's Cortex-A53 (BCM2837) ships without the
# ARMv8 Crypto Extensions, so MBEDTLS_AESCE_C and the SHA2 *_USE_ARMV8_A_CRYPTO
# options would fault with SIGILL; the portable C code is used instead.
# Entropy is the platform's own /dev/urandom reader (entropy_poll.c).
set -euo pipefail
cd "$(dirname "$0")/.."
. tools/common.sh

SRC="$TP/mbedtls"
fl_require "$SRC/library/ssl_tls.c"
OBJ="$BUILD/obj/mbedtls"
rm -rf "$OBJ"; mkdir -p "$OBJ/cfg"

CFG="$OBJ/cfg/mbedtls_config.h"
cp "$SRC/include/mbedtls/mbedtls_config.h" "$CFG"
# (scripts/config.py needs the mbedtls framework submodule a shallow clone lacks, so
# the one stock-enabled Arm option is commented out with sed. The *_USE_ARMV8_A_CRYPTO
# options are off in the stock file.)
sed -i '' -e 's|^#define MBEDTLS_AESCE_C|//#define MBEDTLS_AESCE_C|' "$CFG"
# 128-bit division is __udivti3 from compiler-rt, which no dylib in this port
# exports; MBEDTLS_NO_UDBL_DIVISION (config.h: "the platform lacks support for
# double-width integer division") makes bignum use 64-bit divides instead.
sed -i '' -e 's|^//#define MBEDTLS_NO_UDBL_DIVISION|#define MBEDTLS_NO_UDBL_DIVISION|' "$CFG"
grep -q '^#define MBEDTLS_NO_UDBL_DIVISION' "$CFG" || { echo "error: could not enable MBEDTLS_NO_UDBL_DIVISION" >&2; exit 1; }
grep -q '^#define MBEDTLS_AESCE_C' "$CFG" && { echo "error: MBEDTLS_AESCE_C still on" >&2; exit 1; }
CF=(-I "$SRC/include" -I "$SRC/library" "-DMBEDTLS_CONFIG_FILE=\"$CFG\"")

fl_stage_headers mbedtls "$SRC"/include/mbedtls/*.h
fl_stage_headers psa "$SRC"/include/psa/*.h
cp "$CFG" "$INC/mbedtls/mbedtls_config.h"   # consumers (curl) must see the same options

group() { # print the library/*.c names of one upstream library
    local f n
    for f in "$SRC"/library/*.c; do
        n=$(basename "$f")
        case "$1:$n" in
            x509:pkcs7.c|x509:x509*.c) echo "$f" ;;
            tls:debug.c|tls:mps_*.c|tls:net_sockets.c|tls:ssl_*.c) echo "$f" ;;
            crypto:pkcs7.c|crypto:x509*.c|crypto:debug.c|crypto:mps_*.c|crypto:net_sockets.c|crypto:ssl_*.c) ;;
            crypto:*) echo "$f" ;;
        esac
    done
}
for lib in crypto x509 tls; do
    O="$OBJ/$lib"; mkdir -p "$O"
    for f in $(group $lib); do fl_compile "$O" "$f" "${CF[@]}"; done
    fl_compile_report "mbed$lib"
done

fl_link_dylib mbedcrypto 16:0:0 "$OBJ/crypto"
fl_link_dylib mbedx509 7:0:0 "$OBJ/x509" "$LIB/libmbedcrypto.dylib" "${FL_NET_DYLIBS[@]}"   # inet_pton: libresolv
fl_link_dylib mbedtls 21:0:0 "$OBJ/tls" "$LIB/libmbedx509.dylib" "$LIB/libmbedcrypto.dylib" "${FL_NET_DYLIBS[@]}"
