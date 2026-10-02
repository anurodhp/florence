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
| `FloPage.m` | page view: paints only the dirty rect into one reusable buffer |
| `FloUI.m` | windows, toolbar, scheduler pump, menus, `main()` |

## Low-power design

* The scheduler pump is a one-shot `NSTimer` that sleeps until NetSurf's next callback (0.3% CPU idle, measured).
* Only the exposed rectangle is rendered; scrolling blits existing pixels (`copiesOnScroll`).
* Window resizes are debounced (80 ms) so a drag reflows once.
* Options: JS off, 8 MB memory cache, no animated images, few fetchers (`FLORENCE_FULL=1` keeps NetSurf's defaults).

## Status

* `FloPage.m`/`FloUI.m` compile against gnustep-gui 0.30 and were run under Xvfb with a fake core: layout, upright
  rendering, wheel scrolling, click coordinates, idle CPU verified.
* `gs_core.c`/`gs_window.c` are written against the NetSurf 3.11 API from memory and have **not** been compiled
  (the NetSurf tree was unreachable when written). Expect a first-compile pass over: `gui_window_table` member
  names/signatures (`get_scroll`, `place_caret`, `event`), `netsurf_init()` arguments, option names in
  `apply_overrides()`, `filepath_generate()`, the `urldb_*` header, and `browser_window_*` signatures. Compare with
  `frontends/monkey/main.c` and `frontends/gtk/window.c`.
* On a GNUstep whose AppKit draws bitmaps upright in flipped views, run with `FLORENCE_NOMIRROR=1`.
* Not done: tabs, bookmarks, downloads, certificate prompts, a download table, favicon, caret blink.
