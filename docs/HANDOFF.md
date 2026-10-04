# Handoff: the WebKit branch (read this first)

Branch `claude/intelligent-einstein-pdi60i`, PR anurodhp/florence#2 (draft). Goal from the user: switch Florence's engine
from NetSurf to WebKit, clone WebKit pinned to a version, hook up the bare minimum of UI. Eventual target: low-CPU,
low-RAM Darwin on a Raspberry Pi 3 (also Linux); turn off every WebKit feature possible; GPU support later, not now.
`docs/webkit-port.md` is the design document; this file is the state of play and the lab notebook.

## What this is, in one paragraph

WebKit itself (the official repo, tag `wpewebkit-2.54.0`, commit `73f39d84ea9d4071994214373efbde3665402b05`), **WPE port**
(the toolkit-less one; WPE is a port of WebKit, not a different engine), built with `config/webkit-options.cmake`. Our code
supplies a custom WPE *display* (`frontend/flo_platform.c`): WebKit's web process paints on the CPU (Skia) into shared
memory, WPE hands each frame to our view, and a GNUstep `NSView` draws the exposed rectangle. GLib's main context is
pumped from NSRunLoop (`FloGLib.m`), no thread and no polling timer. The NetSurf engine and its full UI are on `master`
(`git show master:frontend/...`).

## State

| Item | State |
|---|---|
| Pin + sparse clone (`setup_third_party.sh`) | works, commit asserted, 1.8 GB |
| 103 build options, `scripts/check_webkit_options.sh` | all exist in the pinned tree; CMake configure passes |
| Full Linux build (`scripts/build_webkit.sh host`) | succeeds (8,400 steps, ~1 h on 4 cores, ~16 GB RAM box) |
| Headless smoke test (`scripts/test_host.sh`) | 7 of 8 checks pass |
| GNUstep UI | compiled (syntax-checked vs gnustep-gui 0.30), **never run** (no X session tried) |
| Pi / Darwin | nothing built. `build_webkit.sh cross` and `build_florence.sh pi` are written, never run |

### Open bugs, in priority order

1. **Mouse wheel does not scroll** (smoke test check 8). Keyboard (Page Down) does. In gdb on the web process, no
   breakpoint in `EventDispatcher::wheelEvent` / `WebPage::wheelEvent` was hit, so the wheel IPC never reaches the web
   process. The WPE events sent equal what `WPEWaylandSeat.cpp:593` sends for a discrete wheel (`precise=FALSE`,
   `is_stop=FALSE`). Tried: precise deltas + a stop event, a pointer-enter first: no change. Was about to break in the
   **UI process** (`WebPageProxy::handleNativeWheelEvent` -> `handleWheelEvent` -> `continueWheelEventHandling` ->
   `sendWheelEvent`, `UIProcess/WebPageProxy.cpp:4812-4900`). `break` by those names said "not defined" under gdb
   (overloads/inlining; the symbols exist in `nm -C`, so use `break *0xADDR` from `nm`, or `rbreak`, or a printf
   patch). Hypotheses: `wheelEventCoalescer().shouldDispatchEvent` returns false; `handleWheelEvent` drops the event on
   non-Mac when `drawingArea()->shouldSendWheelEventsToEventDispatcher()` is false (it is `true` for
   `DrawingAreaProxyCoordinatedGraphics.h:92`, but check which drawing area class is actually in use); or the event
   signal is not routed for SCROLL. Compare with the working click path (`WPEWebViewPlatform.cpp:384`).
2. **~60 frames/s idle repaint**, damage is always `21x600 at 779,0` (the vertical scrollbar) once a page taller than
   the viewport is shown (`tests/pages/second.html`). `WEBKIT_DISPLAY_REFRESH_THROTTLE_FPS=30` has no effect. Suspect:
   our edit disables *threaded scrolling* (`setThreadedScrollingEnabled(false)`) while `ENABLE_ASYNC_SCROLLING` is built in,
   so `ScrollbarsControllerCoordinated` / `ScrollingCoordinatorCoordinated` are in a mixed state. Bugs 1 and 2 probably
   share this cause. **First experiment: rebuild with `ENABLE_ASYNC_SCROLLING OFF`** in the options file (it is listed
   under "Left on" there; move it). Also check whether the page keeps repainting with no scrollbar (first.html).
   (`TRACE_FRAMES=1 scripts/test_host.sh` prints damage rects.)
3. Unmeasured: stripped library size and RSS on a Pi (the Linux library is 114 MB unstripped).
4. Unverified: the GNUstep UI at runtime; `FloGLib.m` uses GNUstep's `addEvent:type:watcher:forMode:` (ET_RDESC/ET_WDESC) which
   compiles but was never exercised; the UI-process threads WebKit creates may reproduce the iokit port's
   `fatal IO error 22` (CLAUDE.md, "Lessons"). Keys carry a keyval but no hardware keycode (`KeyboardEvent.code` empty).
   Default keymap needs xkeyboard-config data at run time (`wpe_keymap_xkb_new`).

## Findings that cost time (do not rediscover)

* **WPE forces GL compositing**: `UIProcess/wpe/WebPreferencesWPE.cpp` hard-codes `AcceleratedCompositingEnabled(true)`
  (status `embedder`, not settable through `WebKitSettings`), so "no GPU" = software GL and the web process aborts in libepoxy
  ("Couldn't open libGLESv2.so.2"). Fix in `scripts/webkit_fixes.sh`: read `WEBKIT_DISABLE_COMPOSITING_MODE` (GTK's
  name, `gtk/HardwareAccelerationManager.cpp:45`) and also set `HardwareAccelerationEnabled(false)`, otherwise
  `AcceleratedSurface::RenderTargetShareableBuffer` still calls GL (`usesGL()` = composited || hardwareAccelerationEnabled).
  `flo_engine_init` sets the variable. With it, no GL library is loaded.
* Options CMake silently ignores if misspelt; `USE_GSTREAMER` is separate from `ENABLE_VIDEO` etc. (configure fails otherwise).
* Upstream does not build the stripped config: `ENABLE_VIDEO=OFF` still compiles `bindings/js/JSHTMLMediaElementCustom.cpp`;
  `USE_LIBDRM=OFF` leaves `DRM_FORMAT_XRGB8888` undeclared at `UIProcess/wpe/AcceleratedBackingStore.cpp:220`. Both fixed by
  `scripts/webkit_fixes.sh` (idempotent, asserts anchors). Expect more with each new option or WebKit tag.
* Many env knobs exist only under `ENABLE(DEVELOPER_MODE)` (our build is not): `WEBKIT_DISABLE_ASYNC_SCROLLING`,
  `WEBKIT_TLS_CAFILE_PEM`, `WEBKIT_EXEC_PATH`, `WEBKIT_PAUSE_WEB_PROCESS_ON_LAUNCH`. Real ones used: `WEBKIT_SKIA_ENABLE_CPU_RENDERING`,
  `WEBKIT_SKIA_CPU_PAINTING_THREADS`, `WEBKIT_FORCE_VBLANK_TIMER`, `WEBKIT_DISPLAY_REFRESH_THROTTLE_FPS`. TLS trust comes from the
  glib-networking backend's own CA path (set at its build time), not from an env var.
* WebKit caches `LIB_INSTALL_DIR`/`EXEC_INSTALL_DIR`/`LIBEXEC_INSTALL_DIR` on first configure: a later different
  `CMAKE_INSTALL_PREFIX` is ignored (and `cmake --install` writes to the old place, e.g. `/usr/local/lib/pkgconfig`). Use a fresh
  build dir. Changing them via `cmake -U` triggered a full rebuild.
* Installed helper processes have their rpath stripped: run with `LD_LIBRARY_PATH=build/webkit-host/root/lib`
  (`test_host.sh` does). Helpers are found via the baked install prefix, not `WEBKIT_EXEC_PATH`.
* `JavaScriptCore/llint/LowLevelInterpreter.cpp` (CLoop) takes ~20 min and blocks everything behind it; `cmake --install`
  also needs the whole tree built (it wants `inspector.gresource`), so build the default target, not three.
* WPE API facts verified in source: `wpe_event_pointer_button_new` asserts press_count == 0 unless type is DOWN;
  `wpe_display_create_toplevel` is transfer-full and the view takes its own ref; after `render_buffer` WebKit sends no new frame
  until `wpe_view_buffer_rendered`; `wpe_view_buffer_released` for the previous buffer; DMA-BUF/EGL paths are never used because
  `get_egl_display` is not implemented. `webkit_settings_set_hardware_acceleration_policy` and back/forward gestures are GTK-only;
  DNS prefetching, hyperlink auditing, offline app cache setters are deprecated no-ops (removed from `flo_engine_init`).

## Reproducing the Linux setup (Ubuntu 24.04, what I used)

```
apt-get install libglib2.0-dev libsoup-3.0-dev libharfbuzz-dev libharfbuzz-icu0 libicu-dev libjpeg-dev libepoxy-dev \
  libgcrypt20-dev libtasn1-6-dev libxkbcommon-dev libxml2-dev libpng-dev libsqlite3-dev libwebp-dev zlib1g-dev \
  libfreetype-dev libfontconfig-dev libbrotli-dev unifdef gettext libsystemd-dev   # + cmake ninja clang ruby perl python3 gperf
apt-get install libgnustep-gui-dev libgnustep-base-dev gobjc gnustep-make gnustep-back0.30   # UI syntax checks
./setup_third_party.sh   # (curl.se was blocked by the sandbox proxy, 403: unrelated to WebKit; the WebKit part ran first)
scripts/build_webkit.sh host && scripts/build_florence.sh host && scripts/test_host.sh
```

Debugging recipes: replace `build/webkit-host/root/libexec/wpe-webkit-2.0/WPEWebProcess` with a shell wrapper doing
`exec gdb -batch -x CMDS --args .../WPEWebProcess.real "$@"` (the process is launched by the UI with inherited fds; stderr
is ours). That is how the libepoxy abort was located (stack: `NonCompositedFrameRenderer::updateRendering` ->
`RenderTargetSHMImage::create` -> epoxy). `strace -f -e trace=execve,openat,write` shows which process opens what.
`nm -C libWPEWebKit-2.0.so.1.11.3` works (not stripped). Restore the real binary afterwards (re-run `cmake --install`).

## Next steps (suggested order)

1. Fix bugs 1 and 2 (try `ENABLE_ASYNC_SCROLLING OFF` first, rebuild, rerun `scripts/test_host.sh`; the smoke test should end
   at 8/8 and idle frames should be ~0: add an assertion for idle frame count to `tests/smoke_engine.c`).
2. Run the UI: `scripts/build_florence.sh host`, `Xvfb :99 &`, `DISPLAY=:99 LD_LIBRARY_PATH=build/webkit-host/root/lib
   build/florence-host/florence tests/pages/first.html`, screenshot (xwd/import); check upright rendering (`FLORENCE_NOMIRROR`),
   mouse, wheel, typing in the address bar and in a page text field, window resize, idle CPU.
3. Measure memory/CPU on Linux (`ps`, `/proc/PID/smaps_rollup` of UI, web and network processes) to decide further option cuts
   (cache model is `DOCUMENT_VIEWER`; page cache off; consider memory-pressure settings, process count).
4. Pi: dependency stack as dylibs in the iokit port (table in `docs/webkit-port.md`; ICU 70.1+ is the awkward one), then
   the first Darwin compile of WPE (expect Linux-isms: process launcher, shared memory, eventfd/memfd, prctl). At that point
   create an `anurodhp/WebKit` fork and replace `scripts/webkit_fixes.sh` (iokit repo rule: forks, no patch files).
5. Re-add UI features on WebKit APIs: tabs, find (`WebKitFindController`), downloads, content blocking
   (`WebKitUserContentFilterStore`; `frontend/assets/blocklist-default.json` is already Safari JSON), context menu, favicons,
   preferences, JS opt-in (`flo_engine_set_javascript`, JS is off at run time; it cannot be compiled out).

## Rules and context

* `CLAUDE.md` is current for this branch (toolchain rules for the Pi, conventions, lessons). Cross builds follow the sibling
  `xnu-iokit-pi3` repo (`../iokit`, not present in the cloud sandbox; `ls /home/user` shows `xnu-iokit-pi3`).
* Do not push to other branches; the PR is a draft; the user asked to keep pushing changes to this branch/PR.
* Sandbox notes: scratch builds lived in `/tmp/claude-0/wk/` (WebKit checkout `t/third_party/webkit`, build dir `bld`), symlinked
  into the repo as `third_party/webkit` and `build/webkit-host` (both gitignored, gone in a new container). Rebuilding from
  scratch is ~1 h. A repo-local `third_party/webkit` made by `setup_third_party.sh` is equivalent.
* Not decided: the repo licence (GPL-2.0-only was chosen because of NetSurf, which is gone from this branch; WebKit is
  LGPL-2/BSD). Left unchanged and flagged in README.

## Branch `claude/webkit-2.54-pi` (2026-10-04): the Pi, WPE 2.54.0, CPU rendering, no GL

Forked from 8fac3c1, before the Safari-610 detour (kept on `claude/intelligent-einstein-pdi60i`: WebKit
`Safari-610.4.3.1.7` pinned, glib 2.66 / harfbuzz / sqlite / epoxy / xkbcommon / psl built for the Pi; abandoned
because that WebKit's WPE port cannot render without EGL + GLES, which the Pi lacks). 2.54 has the Skia CPU path
(`docs/webkit-port.md`), so no GL. The 610-era sources and staged tree were moved aside
(`third_party/_610`, `build/root-610`, both gitignored).

Carried over from the other branch (version independent): `tools/meson_cross.sh` (Meson cross file, pkg-config
staging, install-name fixing), `compat/` + `scripts/build_compat.sh` (`libflocompat.dylib`: libSystem exports the iokit
port lacks; each has an urgent bug in the DarwinOS Plane project), `scripts/stage_iokit_libs.sh`, `scripts/deps_fixes.sh`,
`scripts/build_pi_test.sh` + `tests/pi/`. The `build_glib/harfbuzz/sqlite/meson_lib` scripts are written for the 610-era
versions (glib 2.66, harfbuzz 2.7.2, libsoup 2): bump the pins for 2.54 (glib >= 2.70, libsoup 3 + nghttp2,
harfbuzz >= 2.7.4, ICU >= 70.1, libxml2 >= 2.9.13, C++23 so libc++ >= 19; see `iokit/docs/webkit-browser-feasibility.md`
section 2 for the matrix and the Darwin hazards). Pi: `ssh root@10.0.0.142`, deploy with `tools/deploy_to_pi.sh`.

Order: (1) libc++ >= 19 (side by side first, under its own install name, then system-wide after the audits; it is on
the boot path), (2) ICU >= 70 and libxml2, (3) glib, libsoup 3, TLS, the leaf libraries, (4) WPE 2.54 compile with
the Darwin compatibility header, (5) the existing glue and UI on the Pi.
