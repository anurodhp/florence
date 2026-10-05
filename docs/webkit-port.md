# The WebKit engine

This branch is the engine **WPE WebKit 2.54.0**, built for software rendering with every
feature that can be left out left out. The GNUstep UI is back to the bare minimum so the engine is
what is being proved. What works, what has been checked and what has not is stated plainly below.

## Why WPE

WebKit has two Linux ports. GTK drags in GTK 4 (a second toolkit next to GNUstep). **WPE** has no
toolkit at all: its "WPE Platform" API lets the embedder supply its own display, and a view then
receives each finished frame as a pixel buffer. Florence supplies a display that has no screen
(`frontend/flo_platform.c`), takes the buffers (shared memory, painted by Skia on the CPU) and draws the
exposed rectangle into a GNUstep view. No GPU, no GL context, no compositor. When a GPU arrives it
is `get_egl_display` plus a DMA-BUF buffer path in that one file.

## Layout

```
GNUstep UI (Objective-C)              engine glue (C, GLib/WebKit headers)        WebKit (C++)
frontend/FloMain.m      menus, main   frontend/flo_engine.c   web view, input     libWPEWebKit-2.0
frontend/FloBrowser.m   window, tabs    frontend/flo_platform.c WPE display, view   WPEWebProcess
                        reload/addr                           and toplevel        WPENetworkProcess
frontend/FloPageView.m  draws frames  frontend/flo.h  <-- the only thing the two sides share
frontend/FloGLib.m      GLib main context driven from NSRunLoop
```

`flo.h` carries plain C types only: input goes in as numbers (pointer, wheel, key), pixels come out as
BGRA, events (title, address, progress, history state, link under the pointer) come back as callbacks.

### One loop

WebKit's UI-process half runs on GLib's default main context. `FloGLib.m` does not poll it and does not
give it a thread: it asks GLib what it waits for (`g_main_context_prepare/query`), registers exactly those
file descriptors and one one-shot timer with GNUstep's run loop, sleeps, and on wake runs
`g_main_context_check/dispatch`. Idle CPU is therefore whatever WebKit itself schedules. This follows
`CLAUDE.md`: nothing runs beside GNUstep's run loop. WebKit does make its own threads inside the UI-process
library (work queues, the network and web processes are separate processes); whether that reproduces the
`fatal IO error 22` the EasyList thread caused is **not known** until it runs on the Pi.

### Frames

The web process paints into shared-memory buffers. `FloView.render_buffer` gets one with its damage
rectangles, and from the main loop (never inside WebKit's own call) makes it current, tells the UI which box
changed, releases the previous buffer and reports the new one rendered, the order WebKit's own headless
view uses. The UI converts only the exposed rectangle to RGB and draws it with the same vertical-mirror
workaround the earlier UI needed for gnustep-gui's bitmap drawing.

## What was turned off

`config/webkit-options.cmake` is the list, grouped by what each switch saves (dependencies, idle
processes and timers, dead code) and followed by what was left on and why. 103 options, each checked
against the pinned tree by `scripts/check_webkit_options.sh` (CMake silently ignores a misspelt
`-D`, which would turn a feature back on). Highlights:

* No JIT (no DFG, no FTL, no WebAssembly): the JavaScriptCore interpreter ("CLoop") is the only tier.
  JavaScript itself cannot be compiled out of WebKit; it is **off at run time** until the user opts in
  (`flo_engine_set_javascript`), and the interpreter's pages are then never touched.
* No GPU process, GBM, DRM, WebGL, WebGPU, Vulkan; no GStreamer, so no `<video>`/`<audio>`/WebAudio/WebRTC/EME.
* No libwpe, Wayland, DRM or headless platform, no ATK, no introspection or docs, no Qt, no Cog.
* No XSLT, MathML, PDF.js, spell checking, geolocation, notifications, gamepad, WebXR, touch, fullscreen,
  pointer lock, smooth scrolling (an animation timer), remote inspector (a socket and a thread), WebDriver.
* Runtime (environment variables set by `flo_engine_init` unless the user already set them): Skia CPU
  rendering, one painting thread (default is half the cores), a 30 fps frame cap on a timer vblank,
  the smallest cache model, no page cache, no ITP database, JavaScript off.
* Kept on deliberately: Safari content-blocker rule lists (`ENABLE_CONTENT_EXTENSIONS`: it is how ads
  will be blocked, and `frontend/assets/blocklist-default.json` is already in that format), drag support
  (which is also mouse text selection), context menus, the periodic memory monitor (the only
  memory-pressure signal on a port with no swap).

What cannot be turned off is listed at the top of the options file: libepoxy, libgcrypt, libtasn1,
libxkbcommon and WebP are `REQUIRED` by `OptionsWPE.cmake` even with everything that uses them off.
Making them optional is a patch, and under the repo rules a patch means an `anurodhp/WebKit` fork.

## Verified, and what that does not prove

Done on Linux (Ubuntu 24.04, x86-64, clang) against the pinned tree:

* `setup_third_party.sh` clones the tag sparsely (1.8 GB instead of 8), refuses a tag that no longer points
  at the pinned commit.
* `scripts/check_webkit_options.sh`: all options exist.
* A real CMake configure with exactly that option set succeeds and `cmakeconfig.h` shows every option
  at the value asked for. The first attempt failed: `USE_GSTREAMER` is a separate switch from the media
  features, and CMake still insisted on GStreamer until it was turned off too.
* The Objective-C files syntax-check against gnustep-gui 0.30.

* The whole engine **compiles and links on Linux** with the option set (`scripts/build_webkit.sh host`, 8,400
  steps, about an hour on four cores). That needed three edits to WebKit (`scripts/webkit_fixes.sh`, anchors asserted):
  * `ENABLE_VIDEO=OFF` still compiled `JSHTMLMediaElementCustom.cpp`, which has no `#if ENABLE(VIDEO)`.
  * `USE_LIBDRM=OFF` left `DRM_FORMAT_XRGB8888` undeclared in `AcceleratedBackingStore.cpp`.
  * **WPE forces GL compositing in the web process** (`WebPreferencesWPE.cpp`), so "no GPU" really means
    software GL: the first run aborted in libepoxy looking for libGLESv2. GTK has `WEBKIT_DISABLE_COMPOSITING_MODE`
    for the non-composited renderer (Skia on the CPU into shared memory); WPE had no equivalent, so the edit gives
    it one, and `flo_engine_init` sets it. With that, no GL library is loaded at all.
* **End to end, with no UI and no GL** (`scripts/test_host.sh`, `tests/smoke_engine.c`, built against the real
  engine): a frame arrives; title and address events fire; pixels are exact (red, blue and white boxes at the
  expected positions); a mouse click on a link loads the second page; Page Down scrolls. 7 of 8 checks pass.
* The Objective-C files syntax-check against gnustep-gui 0.30 (they have not been run: no X session was used).

**Known problems found by that run, not fixed:**

* **The mouse wheel does not scroll** in non-composited mode (the keyboard does). In the UI process the wheel
  handler (`WebPageProxy::handleNativeWheelEvent` onward) was being traced when work stopped; the web process
  never saw a wheel message. The WPE events sent are the same as the Wayland platform's for a discrete wheel.
* **The page repaints at about 60 frames per second while idle**, always the 21-pixel scrollbar strip on the
  right (`WEBKIT_DISPLAY_REFRESH_THROTTLE_FPS=30` did not slow it). Probably the same cause: threaded/async
  scrolling is off in the web process (`setThreadedScrollingEnabled(false)`) but `ENABLE_ASYNC_SCROLLING` is built
  in, so the coordinated scrollbar controller is in a mixed state. Candidate fixes: build with
  `ENABLE_ASYNC_SCROLLING=OFF` (not yet tried), or fix the scrollbar path. This is the main idle-CPU cost to remove
  before the Pi.
* The Linux build is unstripped (the library is 114 MB); the size on the Pi is unmeasured.

**Not done, and not claimed:** nothing here has been built for or run on Darwin or the Pi.

## What the Pi build still needs

`scripts/build_webkit.sh cross` configures with the iokit toolchain and stops at the first missing
dependency. The iokit port provides cairo, freetype, fontconfig, libpng, libxml2, ICU, libffi, expat,
zlib and X11. WPE 2.54 needs, from `Source/cmake/OptionsWPE.cmake` with the options above:

| Needed | Minimum | In the iokit port? |
|---|---|---|
| GLib (glib, gobject, gio, gmodule, gthread) | 2.70 | no |
| libsoup 3 and a GIO TLS backend (glib-networking: GnuTLS or OpenSSL, with the CA file at `/usr/local/share/florence/cacert.pem`) | 3.0 | no |
| SQLite | any | no |
| HarfBuzz (with its ICU glue) | 2.7.4 | no |
| ICU | **70.1** | **66.1: too old** |
| libjpeg (libjpeg-turbo) | any | `build_cmake_lib.sh libjpeg-turbo` |
| libwebp (with demux) | any | no |
| libgcrypt, libtasn1 | 1.7 | no |
| libepoxy | 1.5.4 | no (never called: no EGL display) |
| libxkbcommon | 0.4 | no, and `xkeyboard-config` data at run time for the default keymap |
| libxml2 | 2.9.13 | yes, version to check |
| FreeType with WOFF2 (needs brotli), else libwoff2 | 2.9 | yes, WOFF2 support to check |
| pcre2, libpsl, nghttp2, brotli | (GLib / libsoup) | no |

Each needs a pin in `setup_third_party.sh` and a `scripts/build_*.sh`, as a dylib. The WebKit sources
themselves will then need porting: this is the WPE (Linux) port, and the first compile on Darwin will show
how much of it assumes Linux (the process launcher, shared memory, `eventfd`/`memfd`, `prctl` appear in the
tree). Under the iokit port's rule every such change is a commit in an `anurodhp/WebKit` fork that
`setup_third_party.sh` then points at; no patch files.

`WEBKIT_DISABLE_ASYNC_SCROLLING`, `WEBKIT_TLS_CAFILE_PEM`, `WEBKIT_EXEC_PATH` and other tuning variables exist only in
developer-mode builds, which this configuration is not; the knobs that are real are in `flo_engine_init`,
each with the source line that reads it.

## Not in the UI yet

The UI is the earlier one (`FloBrowser`, `FloToolbar`, `FloStore`, `FloPrefs`, `FloAbout`, from the tag `legacy-engine`) on a new tab
model (`FloTab` owns a `flo_page`). In: tabs, bookmarks, history, find (`WebKitFindController`), zoom, preferences, the
content blocker (`flo_engine_set_blocklist`: `WebKitUserContentFilterStore`, compiled once and cached under the data
directory), links that open a new tab, the start page (`FloStartPage.m`: HTML and CSS tiles written from the bookmark and
history files), downloads (`WebKitNetworkSession::download-started`, saved under the "downloads" setting, shown in the status
label), a context menu and Cmd-click (WebKit's own context menu is not implemented for WPE, so the UI builds one from the last
`mouse-target-changed`), and the clipboard (the page's copy and paste go through WPE's clipboard, mirrored to GNUstep's pasteboard
by `FloPageView`). Scroll bars are WebCore's overlay ones, drawn into the frame after a scroll. Not in: favicons (the WPE API has
no getter in this build), and WebKit has no switch for Do Not Track, Referer, a cache size or image animation, so the UI does not
offer them; the EasyList updater (a child process at `legacy-engine`) is not ported. Keyboard events carry a keyval
but no hardware keycode, so `KeyboardEvent.code` is empty; popups are refused; the clipboard is WPE's
in-process one.
