#!/bin/bash
# Fetches the pinned third-party sources into third_party/ (gitignored).
# Re-running skips what exists; --force wipes and re-fetches.
set -e
cd "$(dirname "$0")"
FORCE=0; [ "${1:-}" = "--force" ] && FORCE=1
mkdir -p third_party

# git repos: name|url|ref
REPOS='
mbedtls|https://github.com/Mbed-TLS/mbedtls.git|v3.6.7
buildsystem|git://git.netsurf-browser.org/buildsystem.git|release/1.10
libwapcaplet|git://git.netsurf-browser.org/libwapcaplet.git|release/0.4.3
libparserutils|git://git.netsurf-browser.org/libparserutils.git|release/0.2.5
libhubbub|git://git.netsurf-browser.org/libhubbub.git|release/0.3.8
libcss|git://git.netsurf-browser.org/libcss.git|release/0.9.2
libdom|git://git.netsurf-browser.org/libdom.git|release/0.4.2
libnsutils|git://git.netsurf-browser.org/libnsutils.git|release/0.1.1
libnslog|git://git.netsurf-browser.org/libnslog.git|release/0.1.3
libnsbmp|git://git.netsurf-browser.org/libnsbmp.git|release/0.1.7
libnsgif|git://git.netsurf-browser.org/libnsgif.git|release/1.0.0
netsurf|git://git.netsurf-browser.org/netsurf.git|release/3.11
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
