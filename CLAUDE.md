# CLAUDE.md

Florence: a browser for small machines. On this branch the engine is WPE WebKit 2.54.0, built
for CPU rendering with every removable feature removed, under a bare-minimum GNUstep UI (the
previous engine and its full UI are on `master`). The target is the Raspberry Pi 3 running
the Darwin/XNU port in the sibling repo `xnu-iokit-pi3` ("the iokit port"); Pi builds are
cross-compiled on a Mac. The engine and the glue also build and run on Linux (`scripts/build_*.sh
host`), which is where they are developed and tested; nothing has been built for the Pi yet.
README.md has the layout and status; `docs/HANDOFF.md` is the state of play for resuming; `docs/webkit-port.md` has the design, the verified/unverified
list and the dependency table the Pi build is waiting on.

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

* The runtime is **Apple's objc4**, not libobjc2: no `-fobjc-runtime=gnustep-2.0`. ARC is not
  used; keep retain/release. Objective-C compiler flags come from the port's installed
  `gnustep-config --objc-flags` (as `build_foundation_smoketest.sh` does), never hand-written:
  they carry `-fconstant-string-class=NSConstantString` (without it `@"..."` needs
  `___CFConstantStringClassReference`, which nothing here provides) and the runtime defines.
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

    ./setup_third_party.sh               # pinned sources; WebKit is a sparse shallow clone of a tag, commit asserted
    scripts/build_webkit.sh host         # Linux: the engine (cmake + ninja; see the script's header for packages)
    scripts/build_florence.sh host       # Linux: build/florence-host/florence
    scripts/build_webkit.sh cross        # the Pi: configure only, stops at the first missing dependency
    scripts/build_florence.sh pi         # the Pi: Florence.app; written, never run
    tools/deploy_to_pi.sh                # PI_HOST/PI_USER/PI_PASS (default password "darwin", test image)

WebKit's options are `config/webkit-options.cmake`, nothing else: add or change one there, run
`scripts/check_webkit_options.sh` (CMake ignores an option name that does not exist and builds with the default,
silently turning a feature back on), and put a reason in the file. Prefer fewer features; a feature stays on only
with a stated reason in the "Left on" list.

## Code conventions

* Plain C glue (`flo_*.c`) talks to WebKit and GLib; Objective-C (`Flo*.m`) never includes their headers.
  The only crossing is `frontend/flo.h` (plain C types: input down, BGRA pixels and event callbacks up).
* Target is underpowered hardware: paint only dirty rects into one reusable buffer, no per-frame
  allocation, no polling timers (GLib's main context is driven from the run loop and sleeps in it, `FloGLib.m`),
  debounce resizes, small caches. Check any change against that.
* A WebKit environment variable or setting goes in `flo_engine_init` with the source line in the pinned tree that
  reads it. Many knobs (`WEBKIT_DISABLE_ASYNC_SCROLLING`, `WEBKIT_TLS_CAFILE_PEM`) exist only under
  `ENABLE(DEVELOPER_MODE)`, which this build is not: check before relying on one.
* New third-party code: a pinned entry in `setup_third_party.sh` (git tag or sha256, plus the commit for a git tag),
  a `scripts/build_*.sh` using `tools/common.sh` helpers, dylib not static archive (the iokit port's
  standing rule: nothing ships fully static except its password tool). Edits to a third-party tree are few,
  scripted, idempotent and assert their anchors (`scripts/webkit_fixes.sh`): a different upstream stops the build instead of building something else. The iokit port's
  rule is a fork (`anurodhp/*`) instead of patches; WebKit's Darwin port will need that, and this file then goes away.
* Licensing: the original code here is MIT (see `LICENSE`, `LICENSES/MIT.txt`; it was GPL-2.0-only while the previous engine was linked,
  and none of that is on this branch). Every original file carries `SPDX-License-Identifier: MIT`; new original files get it.
  Third-party and derived files keep their own licence and say so in their own header (never relabel them MIT):
  `compat/include/epoxy/egl.h` (MacPorts stub), the three Apple-derived `compat/include`
  headers (`libproc.h`, `sys/proc_info.h`, `sys/random.h`: APSL-2.0), `tools/bind_audit.sh` (vendored from the iokit repo), the Lucide icons, and the text `scripts/webkit_fixes.sh` patches into WebKit (WebKit's LGPL-2/BSD). The full
  list is in `LICENSE`.

## Lessons learned the hard way

* Do not run threads beside GNUstep's run loop on this port. A worker thread running libcurl (the first EasyList
  updater on `master`) made the browser die within seconds with `fatal IO error 22` on the X connection; moving the
  work into a child process (posix_spawn) fixed it. Background work is a child process. The cause was never pinned
  down. WebKit runs its network and web content in child processes already, but its UI-process library makes some
  threads of its own: if `fatal IO error 22` returns, this is the first suspect.
* Upstream does not test the stripped configuration. Building it found two missing guards (`ENABLE_VIDEO=OFF`
  still compiles `JSHTMLMediaElementCustom.cpp`; `USE_LIBDRM=OFF` leaves `DRM_FORMAT_XRGB8888` undeclared in
  `AcceleratedBackingStore.cpp`), fixed by `scripts/webkit_fixes.sh`. Expect more with each option turned off
  and with every new WebKit tag; the full compile is the only test, and it takes about an hour on four cores
  (JavaScriptCore's `LowLevelInterpreter.cpp` alone is ~20 minutes and blocks everything behind it).
* WebKit caches `LIB_INSTALL_DIR`, `EXEC_INSTALL_DIR` and `LIBEXEC_INSTALL_DIR` on the first configure of a build
  directory (`OptionsWPE.cmake`), so a different `CMAKE_INSTALL_PREFIX` later does not take effect (and `cmake
  --install` writes to the old one). Use a fresh build directory, or `cmake -U` those three, which rebuilds everything.
* A failed `cmake -C` option does not fail: see Build. The first configure of this option set stopped on
  GStreamer although every media feature was off, because `USE_GSTREAMER` is a separate switch.
* WebKit's `*_DEFAULT`s depend on the CPU and OS (`WebKitFeatures.cmake`): on arm64 the JIT, FTL and WebAssembly
  default ON and the interpreter OFF. The options file sets them all explicitly.
* WPE's render path wants its protocol kept: after `render_buffer` WebKit sends no further frame until
  `buffer-rendered`, and the previous buffer must be reported `released` (`flo_platform.c`).
* The old lessons (the previous engine's table lifetime, `VLDTARGET`, the `fetch_start()` hook) live in
  `git show master:CLAUDE.md`; the diagnostics of that UI (`FLORENCE_TRACE`, the X IO error handler, the crash
  report) were not carried over and may be wanted on the first Pi run (`git show master:frontend/gs_core.c`).

## Verifying without the Pi

* The Objective-C side can be syntax-checked and run on Linux with the `gnustep-*` packages
  plus `xvfb`: build `FloUI.m`/`FloPage.m` with `gcc -x objective-c` against a fake core that
  implements `flo_core_*`/`flo_win_*` (draw a red box top-left, a blue one lower). That is how
  orientation (gnustep-gui draws bitmaps bottom-up even in flipped views, hence the mirror in
  `FloPage.m`), scrolling and click coordinates were checked. It proves nothing about the iokit
  port's own GNUstep build.

## Branches

Default branch is `master` (`main` is retired). Do not open pull requests unless asked.
