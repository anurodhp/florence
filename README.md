# Florence

NetSurf 3.11 (no JavaScript) with a GNUstep UI, for small machines (Raspberry Pi 3, Darwin).

    ./setup_third_party.sh                 # fetch pinned sources
    scripts/build_mbedtls.sh && scripts/build_curl.sh && scripts/build_jpeg.sh
    scripts/build_netsurf_libs.sh
    scripts/build_netsurf.sh monkey        # headless smoke test (nsmonkey)
    GNUSTEP_ROOT=/path/to/gnustep scripts/build_netsurf.sh gnustep   # -> usr/local/bin/nsgnustep

## Layout of `frontend/` (copied to `netsurf/frontends/gnustep/`)

| File | Role |
|---|---|
| `gs_core.c` | start-up/shutdown, options (low-memory overrides), resource path, misc table |
| `gs_window.c` | `gui_window_table`, clipboard table, and the `flo_win_*` bridge: the only place NetSurf types meet UI input |
| `gs_plot.c`, `gs_layout.c`, `gs_bitmap.c` | cairo plotters, text measuring, bitmaps |
| `gs_schedule.c`, `gs_fetch.c`, `gs_filetype.c` | from the monkey frontend |
| `gs_download.c` | downloads: saved to `~/Downloads` (safe unique names, progress on the status line) |
| `FloPage.m` | page view: paints only the dirty rect into one reusable buffer |
| `FloTab.m` / `FloBrowser.m` | a tab (one NetSurf window) / a browser window: toolbar, tab strip, status line |
| `FloStore.m` | bookmark and history lists (`~/.netsurf/Bookmarks`, `History`) |
| `FloUI.m` | scheduler pump, the `flo_ui_*` bridge, menus, `main()` |
| `assets/` | the Florentine giglio icon (SVG; `tools/make_icon.sh` renders the PNG/TIFF) |

## Features

Tabs (strip shown with two or more; Cmd-T / Cmd-W, Cmd-{ / Cmd-}), bookmarks (Cmd-D toggles) and history menus,
downloads, an app icon. Bad certificates are handled by the core (`about:query/ssl`: Proceed / Back to safety).
Optional JavaScript: `scripts/build_nsgenbind.sh` once (host tool; needs bison >= 3 and flex, `brew install bison flex`
on a Mac), then `FLO_JS=1 scripts/build_netsurf.sh gnustep`. It stays off until **View > Enable JavaScript** (remembered
in `~/.netsurf/Choices`, or `FLORENCE_JS=1`); expect it to be slow on a Pi 3 and sites that need a modern engine to still fail.

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

Florence's own source files are MIT licensed; see `LICENSE` and `LICENSES/`. The program you build is
**GPL-2.0-only**, because it is one executable that links NetSurf (GPL-2.0-only), and a few files here are
derived from NetSurf's own frontends and keep that licence. `LICENSE` lists exactly which files are which.
