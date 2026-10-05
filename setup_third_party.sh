#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# Fetches the pinned third-party sources into third_party/ (gitignored).
# Re-running skips what exists; --force wipes and re-fetches.
set -e
cd "$(dirname "$0")"
FORCE=0; [ "${1:-}" = "--force" ] && FORCE=1
mkdir -p third_party

# git repos: name|url|ref
# (mbedtls, curl and jpeg below are left over from the NetSurf engine on master; WebKit's network
# stack is libsoup and its JPEG decoder wants libjpeg-turbo, so nothing here uses them yet.)
REPOS='
mbedtls|https://github.com/Mbed-TLS/mbedtls.git|v3.6.7
'
echo "$REPOS" | while IFS='|' read -r name url ref; do
    [ -z "$name" ] && continue
    d="third_party/$name"
    if [ -d "$d" ]; then
        [ "$FORCE" = 1 ] && rm -rf "$d" || { echo "= $d exists"; continue; }
    fi
    echo "+ $name @ $ref"
    git clone -q --depth 1 --branch "$ref" "$url" "$d"
done

# The rendering engine: WPE WebKit, pinned to a release tag AND the commit that tag points at
# (a moved tag aborts). A full checkout is 8 GB, most of it tests; the sparse set below leaves out
# everything the build never reads (LayoutTests, JSTests, ...) and blobs are fetched on demand.
# Bump WEBKIT_TAG and WEBKIT_COMMIT together:  git ls-remote --tags https://github.com/WebKit/WebKit.git 'wpewebkit-*'
WEBKIT_URL=https://github.com/WebKit/WebKit.git
WEBKIT_TAG=wpewebkit-2.54.0
WEBKIT_COMMIT=73f39d84ea9d4071994214373efbde3665402b05
d=third_party/webkit
if [ -d "$d" ]; then
    [ "$FORCE" = 1 ] && rm -rf "$d" || echo "= $d exists"
fi
if [ ! -d "$d" ]; then
    echo "+ webkit @ $WEBKIT_TAG"
    git clone -q -c advice.detachedHead=false --depth 1 --filter=blob:none --no-checkout --branch "$WEBKIT_TAG" "$WEBKIT_URL" "$d"
    got="$(git -C "$d" rev-parse HEAD)"
    [ "$got" = "$WEBKIT_COMMIT" ] || { echo "error: $WEBKIT_TAG is $got, expected $WEBKIT_COMMIT" >&2; rm -rf "$d"; exit 1; }
    git -C "$d" sparse-checkout set --no-cone '/*' '!/LayoutTests' '!/JSTests' '!/PerformanceTests' \
        '!/ManualTests' '!/WebDriverTests' '!/Websites'
    git -C "$d" checkout -q
    echo "  $(du -sh "$d" | cut -f1) checked out"
fi

# libc++ / libc++abi / libunwind for the Pi: WebKit 2.54 is C++23 and the iokit port ships LLVM 11's. LLVM 20.1.8
# (close to the daily Xcode's clang 21, so the headers and the compiler agree). Sparse: only the runtimes.
LLVM_TAG=llvmorg-20.1.8
LLVM_COMMIT=87f0227cb60147a26a1eeb4fb06e3b505e9c7261
d=third_party/llvm-project
if [ -d "$d" ]; then
    [ "$FORCE" = 1 ] && rm -rf "$d" || echo "= $d exists"
fi
if [ ! -d "$d" ]; then
    echo "+ llvm-project @ $LLVM_TAG (runtimes only)"
    git clone -q -c advice.detachedHead=false --depth 1 --filter=blob:none --no-checkout --branch "$LLVM_TAG" https://github.com/llvm/llvm-project.git "$d"
    got="$(git -C "$d" rev-parse HEAD)"
    [ "$got" = "$LLVM_COMMIT" ] || { echo "error: $LLVM_TAG is $got, expected $LLVM_COMMIT" >&2; rm -rf "$d"; exit 1; }
    git -C "$d" sparse-checkout set --cone libcxx libcxxabi libunwind libc runtimes cmake llvm/cmake llvm/utils/llvm-lit third-party/benchmark
    git -C "$d" checkout -q
    echo "  $(du -sh "$d" | cut -f1) checked out"
fi

# release tarballs: name|url|sha256 (curl's configure only generates curl_config.h;
# a git tag has no configure script)
TARBALLS='
curl|https://curl.se/download/curl-8.15.0.tar.xz|6cd0a8a5b126ddfda61c94dc2c3fc53481ba7a35461cf7c5ab66aa9d6775b609
jpeg|https://ijg.org/files/jpegsrc.v9f.tar.gz|04705c110cb2469caa79fb71fba3d7bf834914706e9641a4589485c1f832565b
icu|https://github.com/unicode-org/icu/releases/download/release-74-2/icu4c-74_2-src.tgz|68db082212a96d6f53e35d60f47d38b962e9f9d207a74cfac78029ae8ff5e08c
glib|https://download.gnome.org/sources/glib/2.78/glib-2.78.6.tar.xz|244854654dd82c7ebcb2f8e246156d2a05eb9cd1ad07ed7a779659b4602c9fae
pcre2|https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.42/pcre2-10.42.tar.bz2|8d36cd8cb6ea2a4c2bb358ff6411b0c788633a2a45dabbf1aeb4b701d1b5e840
libxml2|https://download.gnome.org/sources/libxml2/2.9/libxml2-2.9.14.tar.xz|60d74a257d1ccec0475e749cba2f21559e48139efba6ff28224357c7c798dfee
harfbuzz|https://github.com/harfbuzz/harfbuzz/releases/download/8.3.0/harfbuzz-8.3.0.tar.xz|109501eaeb8bde3eadb25fab4164e993fbace29c3d775bcaa1c1e58e2f15f847
libsoup|https://download.gnome.org/sources/libsoup/3.4/libsoup-3.4.4.tar.xz|291c67725f36ed90ea43efff25064b69c5a2d1981488477c05c481a3b4b0c5aa
nghttp2|https://github.com/nghttp2/nghttp2/releases/download/v1.58.0/nghttp2-1.58.0.tar.xz|4a68a3040da92fd9872c056d0f6b0cd60de8410de10b578f8ade9ecc14d297e0
brotli|https://github.com/google/brotli/archive/refs/tags/v1.1.0.tar.gz|e720a6ca29428b803f4ad165371771f5398faba397edf6778837a18599ea13ff
libjpeg-turbo|https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.0.1/libjpeg-turbo-3.0.1.tar.gz|22429507714ae147b3acacd299e82099fce5d9f456882fc28e252e4579ba2a75
shared-mime-info|https://gitlab.freedesktop.org/xdg/shared-mime-info/-/archive/2.4/shared-mime-info-2.4.tar.gz|531291d0387eb94e16e775d7e73788d06d2b2fdd8cd2ac6b6b15287593b6a2de
openssl|https://github.com/openssl/openssl/releases/download/openssl-3.0.15/openssl-3.0.15.tar.gz|23c666d0edf20f14249b3d8f0368acaee9ab585b09e1de82107c66e1f3ec9533
glib-networking|https://download.gnome.org/sources/glib-networking/2.78/glib-networking-2.78.0.tar.xz|52fe4ce93f7dc51334b102894599858d23c8a65ac4a1110b30920565d68d3aba
woff2|https://github.com/google/woff2/archive/refs/tags/v1.0.2.tar.gz|add272bb09e6384a4833ffca4896350fdb16e0ca22df68c0384773c67a175594
libgpg-error|https://www.gnupg.org/ftp/gcrypt/libgpg-error/libgpg-error-1.47.tar.bz2|9e3c670966b96ecc746c28c2c419541e3bcb787d1a73930f5e5f5e1bcbbb9bdb
libgcrypt|https://www.gnupg.org/ftp/gcrypt/libgcrypt/libgcrypt-1.10.3.tar.bz2|8b0870897ac5ac67ded568dcfadf45969cfa8a6beb0fd60af2a9eadc2a3272aa
libtasn1|https://ftp.gnu.org/gnu/libtasn1/libtasn1-4.19.0.tar.gz|1613f0ac1cf484d6ec0ce3b8c06d56263cc7242f1c23b30d82d23de345a63f7a
libwebp|https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.3.2.tar.gz|2a499607df669e40258e53d0ade8035ba4ec0175244869d1025d460562aa09b4
libepoxy|https://github.com/anholt/libepoxy/releases/download/1.5.4/libepoxy-1.5.4.tar.xz|0bd2cc681dfeffdef739cb29913f8c3caa47a88a451fd2bc6e606c02997289d2
libxkbcommon|https://xkbcommon.org/download/libxkbcommon-1.6.0.tar.xz|0edc14eccdd391514458bc5f5a4b99863ed2d651e4dd761a90abf4f46ef99c2b
libpsl|https://github.com/rockdaboot/libpsl/releases/download/0.21.5/libpsl-0.21.5.tar.gz|1dcc9ceae8b128f3c0b3f654decd0e1e891afc6ff81098f227ef260449dae208
sqlite|https://www.sqlite.org/2023/sqlite-autoconf-3440000.tar.gz|b9cd386e7cd22af6e0d2a0f06d0404951e1bef109e42ea06cc0450e10cd15550
'
echo "$TARBALLS" | while IFS='|' read -r name url sha; do
    [ -z "$name" ] && continue
    d="third_party/$name"
    if [ -d "$d" ]; then
        [ "$FORCE" = 1 ] && rm -rf "$d" || { echo "= $d exists"; continue; }
    fi
    echo "+ $name from $url"
    f="third_party/.$name.tar"
    curl -fsSL -o "$f" "$url"
    [ -n "$sha" ] && { echo "$sha  $f" | shasum -a 256 -c - >/dev/null || { echo "error: $name sha256 mismatch" >&2; exit 1; }; }
    echo "  sha256 $(shasum -a 256 "$f" | cut -d' ' -f1)"
    mkdir -p "$d"
    tar -xf "$f" -C "$d" --strip-components=1
    rm -f "$f"
done

# CA roots for TLS verification (curl.se's Mozilla bundle).
mkdir -p third_party/ca
[ -f third_party/ca/cacert.pem ] || curl -fsSL -o third_party/ca/cacert.pem https://curl.se/ca/cacert.pem
echo "done"
