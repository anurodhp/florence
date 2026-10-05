# Florence

A small web browser for small machines. This branch runs it on **WPE WebKit 2.54.0** with a native GNUstep
user interface: the engine is built with every feature that can be left out left out, renders on the CPU
(no GPU yet) and hands finished frames to a GNUstep view. The previous engine with its full UI
(tabs, bookmarks, start page, content blocker, preferences) is on `master`.

The target is a **Raspberry Pi 3 running the Darwin/XNU port** in the sibling repository `xnu-iokit-pi3` ("the
iokit port"), 1 GB of RAM and a slow CPU; it should also run on Linux. **Status: the engine compiles on Linux with the option set (needing three small edits, `scripts/webkit_fixes.sh`) and
passes an end-to-end smoke test there (`scripts/test_host.sh`: frame, pixels, click navigation; the mouse wheel does not
scroll yet and idle repaint is too high); the GNUstep UI has only been compiled, not run; nothing has been built for
Darwin or run on the Pi.** `docs/webkit-port.md`
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
    scripts/build_florence.sh host        # build/florence-host/florence; run under an X server (LD_LIBRARY_PATH=build/webkit-host/root/lib)
    scripts/test_host.sh                  # engine end-to-end check, no UI, no X

For the Raspberry Pi (cross-compiled on a **Mac** with the iokit port's toolchain; see `CLAUDE.md`):
`scripts/build_webkit.sh cross` and `scripts/build_florence.sh pi` exist but **do not work yet**: the port does not
build glib, libsoup, sqlite, harfbuzz and the rest of WPE's dependencies, and its ICU is too old. The table is in
`docs/webkit-port.md`. `tools/deploy_to_pi.sh` copies `build/root` to the Pi when there is one.

## License

The original code in this repository is MIT, see `LICENSE` and `LICENSES/MIT.txt`. It was GPL-2.0-only while the previous engine was
linked; none of that engine, nor of the files derived from its frontends, is on this branch (the UI on `master` keeps its GPL
terms). Third-party code keeps its own licence: WebKit is LGPL-2 and BSD (linked as shared libraries, fetched by
`setup_third_party.sh`), and the few vendored or derived files are listed under "Exceptions" in `LICENSE`.
