# Florence

A small web browser for small machines. This branch runs it on **WPE WebKit 2.54.0** with a native GNUstep
user interface: the engine is built with every feature that can be left out left out, renders on the CPU
(no GPU yet) and hands finished frames to a GNUstep view. The previous engine, NetSurf 3.11 with its full
UI (tabs, bookmarks, start page, content blocker, preferences), is on `master`.

The target is a **Raspberry Pi 3 running the Darwin/XNU port** in the sibling repository `xnu-iokit-pi3` ("the
iokit port"), 1 GB of RAM and a slow CPU; it should also run on Linux. **Status: the engine configures and
compiles on Linux with the option set; nothing has been built for Darwin or run on the Pi.** `docs/webkit-port.md`
says what has been checked, what the Pi build still needs, and what is missing.

## What there is

* `setup_third_party.sh` pins WPE WebKit to the tag `wpewebkit-2.54.0` and to the commit it points at, cloned
  sparsely (1.8 GB instead of 8).
* `config/webkit-options.cmake`: the 100-odd build options, grouped by what each saves, with what was left on
  and why. `scripts/check_webkit_options.sh` proves each still exists in the pinned tree.
* `frontend/`: the bare-minimum browser. One window: Back, Forward, Reload/Stop, an address field (anything that
  is not an address is a DuckDuckGo search, the JavaScript-free page), the page, a status line.
  JavaScript is off.

| File | Role |
|---|---|
| `flo.h` | the one interface between the UI and the engine glue: plain C types |
| `flo_engine.c` | the WebKit web view, settings, input, and GLib's main context exposed for the UI to pump |
| `flo_platform.c` | the WPE display, toplevel and view that receive frames (no GPU, no compositor) |
| `FloGLib.m` | drives GLib's main context from GNUstep's run loop: no thread, no polling timer |
| `FloPageView.m` | draws the exposed rectangle of the newest frame; sends mouse, wheel and key input |
| `FloWindow.m`, `FloMain.m` | the window and its controls; menus and `main()` |
| `assets/` | the icon, the toolbar glyphs, and a starter content-blocker list (Safari JSON, which WebKit reads natively) |

## Building

On Linux, to develop and test the glue and UI (needs the distribution's WPE build dependencies, cmake, ninja,
and gnustep-base/gui; the package list is at the top of `scripts/build_webkit.sh`):

    ./setup_third_party.sh
    scripts/build_webkit.sh host          # the engine: long (WebKit), one JavaScriptCore file alone takes minutes
    scripts/build_florence.sh host        # build/florence-host/florence; run under an X server

For the Raspberry Pi (cross-compiled on a **Mac** with the iokit port's toolchain; see `CLAUDE.md`):
`scripts/build_webkit.sh cross` and `scripts/build_florence.sh pi` exist but **do not work yet**: the port does not
build glib, libsoup, sqlite, harfbuzz and the rest of WPE's dependencies, and its ICU is too old. The table is in
`docs/webkit-port.md`. `tools/deploy_to_pi.sh` copies `build/root` to the Pi when there is one.

`scripts/build_mbedtls.sh`, `build_curl.sh` and `build_jpeg.sh` are left over from the NetSurf engine (WebKit's
network stack is libsoup); `scripts/build_tests.sh` builds their smoke test.

## License

The code in this repository is GPL-2.0-only (version 2 only, no "or later"), see `LICENSE` and
`LICENSES/GPL-2.0-only.txt`. That choice was made because the NetSurf core was linked and some files derived from
NetSurf's frontends; none of those files are on this branch. WebKit itself is under LGPL-2 and BSD licences (see
`third_party/webkit` after setup); whether this repository's licence should change now is **not decided here**.
