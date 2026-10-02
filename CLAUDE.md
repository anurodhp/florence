# CLAUDE.md

Florence: NetSurf 3.11 (no JavaScript) with a GNUstep UI, for small machines. The target is
the Raspberry Pi 3 running the Darwin/XNU port in the sibling repo `xnu-iokit-pi3` ("the iokit
port"). Anything built here is cross-compiled on a Mac and deployed to the Pi; this repo
cannot be built or run on Linux. README.md has the layout and status.

## Read the iokit port before touching the build

`xnu-iokit-pi3` is the reference for every cross-compile decision. Default location is
`../iokit` (override with `IOKIT_DIR`; the checkout directory name is not significant). Read,
in that repo: its `CLAUDE.md`, `tools/userland_staging/gnustep_common.sh` (the GNUstep recipe),
`build_gnustep_terminal.sh` / `build_gnustep_ink.sh` (apps, the closest template to Florence),
`build_cairo.sh`, and `architecture.md`'s GNUstep section. Do not re-derive these rules from
Linux/macOS habits; they are wrong here.

## Toolchain (`tools/common.sh`, same recipe as the iokit port)

* Compile with **Xcode 12's clang** and its **iPhoneOS 14.4 SDK**, `-target arm64-apple-ios14.4`,
  clang invoked directly (never `xcodebuild`). Link with the daily-driver Xcode's newer `ld`
  (`-B$NEWLD_BINDIR -Wl,-fixup_chains`; Xcode 12's ld crashes on fixup chains).
* `-fno-builtin -fno-stack-protector -D_FORTIFY_SOURCE=0` are load-bearing (the SDK's fortify
  inlines recurse; `-fno-builtin` keeps memcpy and friends calling the port's arm64 routines).
* Link `-nostdlib` against the iokit port's own dylibs (`$IOKIT_LIBC/system`, `libc_build`), owners
  first, `libSystem.B` last. Never link the SDK's `.tbd` stubs (`-Wl,-Z` for GNUstep links).
* Every dylib/executable must pass the iokit port's bind audit (`bind_target_audit.sh`; this
  repo's vendored `tools/bind_audit.sh` for non-GNUstep images). A clean link can still abort in
  dyld on a symbol no dylib exports; the audit is not optional.
* The build host's tools (pkg-config, Homebrew/MacPorts/XQuartz `*-config`) must not leak in.

## GNUstep specifics

* The runtime is **Apple's objc4**, not libobjc2: no `-fobjc-runtime=gnustep-2.0`, no
  `-fconstant-string-class` (that probe fails here). ARC is not used; keep retain/release.
* GNUstep is installed by the iokit port in gnustep-make's "gnustep" layout under
  `libc_build/gnustep/root/usr/GNUstep/System/{Library,Applications,Tools}`; the image rearranges
  it to the Mac layout (`/Applications/X.app`, `/System/Library`, `/usr/local/bin`) with
  `/etc/GNUstep/GNUstep.conf`. Libraries keep their `/usr/GNUstep/...` install names.
* cairo, pixman, freetype, fontconfig, libpng, X11 dylibs are in `libc_build/system`; their headers
  in `libc_build/x11/include` (`cairo/`, `freetype2/`). objc4 headers: `libc_build/objc4_public_headers`.
* Do not copy the link logic: `tools/gnustep_env.sh` sources the port's `gnustep_common.sh` in a
  subshell and takes `gs_setup_link`'s results (`LINK`, `LIBFLAGS`: -l names, `-dylib_file` maps
  for libSystem's re-exports). Keep following the port; if its recipe changes, this follows.
* No `NSApplicationMain` here: `main()` calls `GSInitializeProcess` itself. The app is staged as a
  bundle `build/root/Applications/Florence.app` (Info-gnustep.plist); run it with `openapp Florence`
  under an X server (gnustep-back cairo on X11 with Window Maker).

## Build

    ./setup_third_party.sh
    scripts/build_mbedtls.sh && scripts/build_curl.sh && scripts/build_jpeg.sh
    scripts/build_netsurf_libs.sh
    scripts/build_netsurf.sh monkey      # headless smoke test; proven on the Pi
    scripts/build_netsurf.sh gnustep     # the browser -> build/root/Applications/Florence.app
    tools/deploy_to_pi.sh                # PI_HOST/PI_USER/PI_PASS

`build_netsurf.sh` compiles the C glue through NetSurf's own make (`frontend/` is copied to
`netsurf/frontends/gnustep/`), compiles the two `.m` files itself, skips NetSurf's link step and
links with this repo's recipe. Logs: `build/netsurf-<frontend>.log`. Build `monkey` first: if it
fails, the problem is below Florence.

## Code conventions

* Plain C glue (`gs_*.c`) talks to NetSurf; Objective-C (`Flo*.m`) never includes NetSurf headers.
  The only crossing is `frontend/gs.h` (`flo_win_*` down, `flo_ui_*` up, plain enums).
* Target is underpowered hardware: paint only dirty rects into one reusable buffer, no per-frame
  allocation, no polling timers (the pump sleeps until NetSurf's next callback), debounce resizes,
  cap caches (`apply_overrides()` in `gs_core.c`). Check any change against that.
* New third-party code: a pinned entry in `setup_third_party.sh` (git tag or sha256), a
  `scripts/build_*.sh` using `tools/common.sh` helpers, dylib not static archive (the iokit port's
  standing rule: nothing ships fully static except its password tool).
* GPL-2.0-only, like NetSurf (see the header of `frontend/gs.h`).

## Verifying without the Pi

* The Objective-C side can be syntax-checked and run on Linux with the `gnustep-*` packages
  plus `xvfb`: build `FloUI.m`/`FloPage.m` with `gcc -x objective-c` against a fake core that
  implements `flo_core_*`/`flo_win_*` (draw a red box top-left, a blue one lower). That is how
  orientation (gnustep-gui draws bitmaps bottom-up even in flipped views, hence the mirror in
  `FloPage.m`), scrolling and click coordinates were checked. It proves nothing about the iokit
  port's own GNUstep build.
* `gs_core.c` / `gs_window.c` need NetSurf 3.11's headers; compare against
  `frontends/monkey/main.c` and `frontends/gtk/window.c` when the first compile complains.

## Branches

Default branch is `master` (`main` is retired). Do not open pull requests unless asked.
