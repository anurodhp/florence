#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
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
