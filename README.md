# Florence

A small web browser for small machines. It is [NetSurf](https://www.netsurf-browser.org/) 3.11 (the
rendering engine, without JavaScript unless you opt in) with a native GNUstep user interface drawn through
cairo: a Safari-style toolbar, tabs, bookmarks and history, a start page of bookmark and recent-site tiles,
find in page, zoom, downloads, a Preferences window, and a built-in ad and tracker blocker that understands
Safari content-blocker lists and downloads EasyList weekly.

The target is a **Raspberry Pi 3 running the Darwin/XNU port** in the sibling repository
`xnu-iokit-pi3` ("the iokit port"). Everything is designed for 1 GB of RAM and a slow CPU: only exposed
rectangles are repainted, nothing polls, caches are capped, resizes are debounced. It is licensed
GPL-2.0-only (see `LICENSE`).

## Building for the Raspberry Pi 3

Florence is cross-compiled on a **Mac** and copied to the Pi; it does not build on the Pi or on Linux. The
build reuses the iokit port's toolchain recipe and its own libc/GNUstep/cairo dylibs, so that port must be
built first.

**You need**

* The iokit port checked out next to this repo (`../iokit`, or set `IOKIT_DIR`) with its userland built:
  `libc_build/system` (libSystem and friends), cairo and the X11 dylibs, and GNUstep installed under
  `libc_build/gnustep/root`.
* Xcode 12 at `/Applications/Xcode-12.app` (clang and the iPhoneOS 14.4 SDK compile everything), plus the
  everyday Xcode selected with `xcode-select` (its newer `ld` links).
* `git`, `curl`, `make` and `perl` (Homebrew, MacPorts and pkg-config on the host are deliberately hidden from the build).
* The Pi booted into the iokit image with an X server and Window Maker (that is where `openapp` runs it).

**Steps**

    ./setup_third_party.sh                       # fetch pinned sources (NetSurf and its libraries, curl, mbedTLS, libjpeg)
    scripts/build_mbedtls.sh && scripts/build_curl.sh && scripts/build_jpeg.sh
    scripts/build_netsurf_libs.sh                # libcss, libdom, hubbub, ...
    scripts/build_netsurf.sh monkey              # headless smoke test; if this fails the problem is below Florence
    scripts/build_netsurf.sh gnustep             # the browser -> build/root/Applications/Florence.app
    PI_HOST=<pi address> tools/deploy_to_pi.sh   # copies build/root to the Pi over ssh

`tools/deploy_to_pi.sh` defaults to user `root`, password `darwin` (the test image's), host `10.0.0.142`;
set `PI_HOST`, `PI_USER`, `PI_PASS` (empty for ssh keys). On the Pi, with an X server running:

    openapp Florence [url]                       # from the serial console: set DISPLAY first
    FLORENCE_TRACE=1 /Applications/Florence.app/Florence http://example.com    # start-up trace to stderr

Optional JavaScript (Duktape, slow on a Pi 3): `scripts/enable_js.sh [--check] [--deploy]`.
Logs are in `build/netsurf-<frontend>.log`. `FL_OPT=-Os` changes the optimisation level (default `-O2`).
See `CLAUDE.md` for the toolchain rules and the lessons behind them.

## Building for another platform

The scripts above are specific to the iokit port's cross toolchain; there is no one-command build for
other systems yet. What is portable, and how you would go about it:

* The browser engine is stock NetSurf 3.11 plus its libraries; the glue (`frontend/gs_*.c`) is plain C
  against NetSurf's frontend API; the UI (`frontend/Flo*.m`) is Objective-C on GNUstep's Foundation/AppKit
  and cairo, and never includes NetSurf headers. The only crossing is `frontend/gs.h`.
* On **Linux or another desktop with GNUstep**: install the GNUstep base/gui/back (cairo backend) packages,
  cairo, libcurl, libjpeg, libpng and NetSurf 3.11's libraries; copy `frontend/` to `netsurf/frontends/gnustep/`
  (the build script shows the two small build-copy edits NetSurf needs: registering `gnustep` in
  `frontends/Makefile.hts`'s `VLDTARGET`, and the `fetch_start()` hook for the content blocker); build the C glue
  with NetSurf's make; compile the `.m` files with `gcc -x objective-c $(gnustep-config --objc-flags)`;
  link with `$(gnustep-config --gui-libs)`, cairo and NetSurf's objects. Take `scripts/build_netsurf.sh`
  as the reference for the exact file list and flags.
* What has been checked there: the UI (`FloUI.m`, `FloPage.m`, the preferences and so on) runs under Xvfb
  on Linux against a fake core, and the C glue compiles against the real NetSurf 3.11 headers (Ubuntu's
  `netsurf` source package is exactly 3.11). A full Linux build of the browser has not been done.
* On a **different Darwin/XNU port or other cross target**: change `tools/common.sh` (compiler, SDK, target,
  system dylibs) and `tools/gnustep_env.sh` (it takes the GNUstep link recipe from the iokit port).

## Layout of `frontend/` (copied to `netsurf/frontends/gnustep/`)

| File | Role |
|---|---|
| `gs_core.c` | start-up/shutdown, options (low-memory overrides), resource path, misc table |
| `gs_window.c` | `gui_window_table`, clipboard table, and the `flo_win_*` bridge: the only place NetSurf types meet UI input |
| `gs_plot.c`, `gs_layout.c`, `gs_bitmap.c` | cairo plotters, text measuring, bitmaps |
| `gs_schedule.c`, `gs_fetch.c`, `gs_filetype.c` | from the monkey frontend |
| `gs_startpage.c` | the start page (bookmark and recent-site tiles; favicons saved as pages load) |
| `gs_search.c` | page search (find in page): reports whether the last find matched |
| `gs_download.c` | downloads: saved to `~/Downloads` (safe unique names, progress on the status line) |
| `gs_blocker.c` / `gs_lists.c` | the content blocker (Safari rule lists) / the weekly EasyList download and its conversion |
| `FloPage.m` | page view: paints only the dirty rect into one reusable buffer |
| `FloTab.m` / `FloBrowser.m` | a tab (one NetSurf window) / a browser window: toolbar, tab strip, hover label |
| `FloToolbar.m` | the Safari-like parts, drawn with vector icons: icon buttons, rounded address bar, tab strip, hover label |
| `FloPrefs.m` | the Preferences window (Cmd-,): General, Privacy, Content Blocking, Advanced; own settings in `~/.netsurf/Florence.conf` |
| `FloStore.m` | bookmark and history lists (`~/.netsurf/Bookmarks`, `History`) |
| `FloUI.m` | scheduler pump, the `flo_ui_*` bridge, menus, `main()` |
| `assets/` | the Florentine giglio icon (SVG; `tools/make_icon.sh` renders the PNG/TIFF); `assets/icons/` the toolbar glyphs (Lucide, ISC/MIT; `tools/make_toolbar_icons.sh`) |

## Features

A Safari-style toolbar (icon buttons, rounded address bar with reload/stop inside and a padlock only once an https page has loaded with its certificate checked (an orange warning mark if you chose to continue past a certificate error), a
bookmark star, `+` for a new tab). Tabs (strip shown with two or more; Cmd-T / Cmd-W, Cmd-{ / Cmd-}), bookmarks (Cmd-D toggles) and history menus,
downloads, an app icon. Bad certificates are handled by the core (`about:query/ssl`: Proceed / Back to safety).
Optional JavaScript: run `scripts/enable_js.sh` (add `--deploy` to copy it to the Pi; `--check` lists what is missing).
It fetches and builds the host tool nsgenbind (needs bison >= 3 and flex: `brew install bison flex` on a Mac), then
does `FLO_JS=1 scripts/build_netsurf.sh gnustep` and checks the result contains Duktape. It stays off until **View > Enable JavaScript** (remembered
in `~/.netsurf/Choices`, or `FLORENCE_JS=1`); expect it to be slow on a Pi 3 and sites that need a modern engine to still fail.

### Page tools

* **Right-click menu:** on a link (Open Link, Open Link in New Tab, Copy Link Address), an image (Open Image in New Tab,
  Copy Image Address), a text field (Cut/Copy/Paste) or selected text (Copy), plus Back/Forward/Reload.
* **Command-click** (Alt on this keyboard setup) or **middle-click** a link: opens it in a background tab
  (add Shift to bring it to the front).
* **Find in page** (Edit > Find..., Cmd-F; Cmd-G / Shift-Cmd-G for next / previous; Esc or Done closes).
* **Zoom** (View > Zoom In / Out / Actual Size, steps of 10 %, 30 %..300 %).
* **View > Block Ads and Trackers** (Safari-format JSON block lists and a weekly EasyList download, on by default, see `docs/content-blocking.md`), **Send Do Not Track**, **Minimum Font Size**. These are
  remembered in `~/.netsurf/Choices`, which keeps only your own choices (the low-power defaults are not written).

## Start page

A new tab or window with no address opens a generated local page (`~/.netsurf/start/index.html`): a row of
bookmark tiles and, below it, recently visited sites (one per site, bookmarked sites left out). Tiles show the
site's favicon, saved as the core delivers it to `~/.netsurf/favicons/<host>.png`, or a coloured initial until one
has been seen. The page is regenerated each time one opens. The address bar stays empty on it.

## Build optimisation

Everything (libraries, NetSurf, the glue, the UI) is built with `-O2`, set once as `FL_OPT` in `tools/common.sh`
(`FL_OPT=-Os scripts/build_netsurf.sh gnustep` to change it). The build prints which `-O` levels the compile
commands actually carried, and warns about any `-O0`.

## Low-power design

* The scheduler pump is a one-shot `NSTimer` that sleeps until NetSurf's next callback (0.3% CPU idle, measured).
* Only the exposed rectangle is rendered; scrolling blits existing pixels (`copiesOnScroll`).
* Window resizes are debounced (80 ms) so a drag reflows once.
* Options: JS off, 8 MB memory cache, no animated images, few fetchers (`FLORENCE_FULL=1` keeps NetSurf's defaults).

## Status

* Builds with `scripts/build_netsurf.sh gnustep` against the iokit port's GNUstep/cairo, deploys with
  `tools/deploy_to_pi.sh`, and **runs on the Pi 3** (window opens, first page loads).
* `FloPage.m`/`FloUI.m` were also run under Xvfb on Linux with a fake core (layout, upright rendering,
  wheel scrolling, click coordinates, idle CPU 0.3%).
* Debugging aids: `FLORENCE_TRACE=1` prints start-up stages to stderr; a SIGSEGV/SIGBUS handler prints the
  fault address, pc/lr and a frame-pointer walk plus `flo_core_init`'s run-time address (slide = that minus
  `nm` of `_flo_core_init`; look addresses up with `nm -n` / `atos`).
* On a GNUstep whose AppKit draws bitmaps upright in flipped views, run with `FLORENCE_NOMIRROR=1`.
* Not done: favicons, caret blink, a download manager window, a bookmarks manager (the menu lists them; Cmd-D removes).
* The JavaScript build was verified up to the compile of NetSurf's JS glue and generated bindings against the real 3.11
  headers on Linux; it has not been cross-built or run on the Pi.

## License

GPL-2.0-only (version 2 only, no "or later"): Florence links NetSurf, whose core has that licence, and some
files derive from NetSurf's frontends. See `LICENSE` and `LICENSES/GPL-2.0-only.txt`.
