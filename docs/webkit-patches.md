# What Florence changes in WebKit, and how to carry it to a new WebKit

Florence builds the official WPE WebKit tag `wpewebkit-2.54.0` (commit `73f39d84ea9d4071994214373efbde3665402b05`, pinned in
`setup_third_party.sh`) with a set of source edits. This file is the list of those edits and the reason for each, so they can be
re-applied to a later WebKit.

* **The edits themselves** live in `scripts/webkit_fixes.sh`: run by `scripts/build_webkit.sh`, idempotent, and each one asserts
  that the text it replaces is exactly what 2.54.0 has. On a different WebKit the script stops at the first edit whose anchor
  moved: that is the to-do list for the port.
* **The resulting diff** against 2.54.0 is saved in `docs/webkit-2.54.0-florence.patch` (27 files). It is a reference, not
  applied by the build: when an anchor no longer matches, it shows what the edit did, and `git apply -3` of it on the new tree
  resolves the edits whose context still matches.
* **Not here:** the build options (`config/webkit-options.cmake`), the CMake tricks for the Darwin target (`scripts/build_webkit.sh`:
  clearing `APPLE` after `project()`, compiler wrappers), and the dependency edits (`scripts/deps_fixes.sh`, e.g. libepoxy's library
  names). `scripts/webkit_frame_timing.sh` is a diagnostic patch, not part of the build.

## Porting to a new WebKit

1. Point `setup_third_party.sh` at the new tag and commit; fetch.
2. `scripts/webkit_fixes.sh`. For each edit that stops: find the code's new location (the reason column below says what it is for),
   check whether upstream fixed it (then delete the edit), else update its anchor and replacement. Repeat until it runs through.
3. `scripts/check_webkit_options.sh` (options that vanished are silently ignored otherwise), then a full build and the Pi checks:
   `tests/pages/bench.html`, a GL readback check (`FLORENCE_READBACK_LOG=1`, below), arstechnica.com and reddit.com.
4. Regenerate the reference diff: `(git -C third_party/webkit diff; git -C third_party/webkit diff --no-index /dev/null
   Source/WTF/wtf/darwin/OSLogPrintStream.cpp) > docs/webkit-<version>-florence.patch`.

The iokit port's rule is a fork (`anurodhp/WebKit`) rather than a patch script; once the Darwin edits stop growing, these become
commits on a branch of that fork and this file becomes its commit list.

## The edits (2.54.0)

### Feature switches that do not compile when off (upstream always builds them on)

| File | Change | Why |
|---|---|---|
| `WebCore/bindings/js/JSHTMLMediaElementCustom.cpp` | wrap the file in `#if ENABLE(VIDEO)` | `ENABLE_VIDEO=OFF` still compiles this hand-written half of a binding that is not generated |
| `WebKit/UIProcess/wpe/AcceleratedBackingStore.cpp` | guard the `DRM_FORMAT_XRGB8888` use with `USE(LIBDRM)` | `USE_LIBDRM=OFF` leaves the constant undeclared |
| `JavaScriptCore/bytecode/InlineCacheCompiler.h` | include `CCallHelpers.h` | JIT build: the class body needs the complete type; small translation units (LLIntOffsetsExtractor) only had a forward declaration |

### Rendering and scrolling modes

| File | Change | Why |
|---|---|---|
| `WebKit/UIProcess/wpe/WebPreferencesWPE.cpp` | read `WEBKIT_DISABLE_COMPOSITING_MODE` (GTK's variable) and turn off accelerated compositing and hardware acceleration | WPE hard-codes GL compositing; this is Florence's CPU path (`FLORENCE_CPU=1`) |
| same file | `WEBKIT_ASYNC_SCROLLING=0` turns off threaded/async scrolling | in GL mode the overlay scroll bars vanished with threaded scrolling; Florence sets it |
| `WebKit/WebProcess/glib/WebProcessGLib.cpp` | no `CRASH()` when there is no EGL display | the CPU path never needs one (MacPorts `patch-egl-display-no-crash`) |
| `WebKit/WebProcess/WebPage/CoordinatedGraphics/AcceleratedSurface.cpp` | **GL frame readback**: `RenderTargetSHMImage::didRenderFrame` reads the framebuffer with `glReadPixels(GL_BGRA)` straight into the shared-memory bitmap instead of `SkSurface::readPixels`; `FLORENCE_SKIA_READBACK=1` restores Skia's path, `FLORENCE_READBACK_LOG=1` logs each readback's time | see "GL readback" below: 2.5-3x the frame rate in GL mode |

### Darwin as a non-Cocoa target (the toolkit-less port on the iokit kernel)

| File | Change | Why |
|---|---|---|
| `WTF/wtf/PlatformWPE.cmake` (several) | swap Linux-only sources for Darwin ones; add `wtf/darwin/OSLogPrintStream.cpp` | the non-Cocoa branches name Linux files |
| `WTF/wtf/darwin/OSLogPrintStream.cpp` (new file) | a stderr implementation | the only one upstream is Objective-C++ (`.mm`, Cocoa); `JSC::Options` uses it on `OS(DARWIN)` |
| `WebCore/crypto/keys/CryptoKeyEC.h`, `CryptoKeyEC.cpp`, `CryptoKeyRSA.h`, `WebCore/crypto/CryptoKey.cpp` | add `!PLATFORM(WPE)` / `PLATFORM(WPE)` next to the existing GTK exits | WebCrypto through gcrypt, not CommonCrypto |
| `WTF/wtf/PlatformHave.h` | `!PLATFORM(WPE)` on the Mach-exception check | POSIX signals, not Mach exceptions |
| `WTF/wtf/PlatformUse.h` | `!PLATFORM(WPE)` on `USE_ACCELERATE` | no Accelerate framework |
| `WebKit/Platform/IPC/Attachment.h`, `Connection.h`, `ConnectionHandle.serialization.in`, `glib/ConnectionGLib.cpp`, `Shared/WebCoreArgumentCoders.serialization.in` | `USE(UNIX_DOMAIN_SOCKETS)` wins over `OS(DARWIN)` | IPC over unix sockets, no Mach ports/XPC (MacPorts `patch-webkit-ipc-darwin-gtk` and friends) |
| `WebKit/Platform/IPC/unix/IPCUtilitiesUnix.cpp`, `Connection.h` | larger AF_UNIX datagram buffers; datagram socketpair | xnu's defaults are 2 KB/4 KB (`uipc_usrreq.c:865`), messages reach 4 KB; there is no `SOCK_SEQPACKET` |
| `WebKit/UIProcess/API/glib/WebKitWebView.cpp` | a compatibility macro for `g_task_return_new_error_literal` | GLib 2.80 API; Florence ships 2.78 |
| `WebKit/UIProcess/API/C/WKPage.cpp` | align the key-event function's guard with its declaration | the port is `__APPLE__` but not `PLATFORM(COCOA)` |
| `WebKit/WebProcess/InjectedBundle/API/c/WKBundlePage.cpp` | ignore `TARGET_OS_IPHONE` | the SDK is iPhoneOS; this is not an iOS port |
| `WebKit/SourcesWPE.txt` (2) | add `ViewGestureController.cpp`, `ViewGestureGeometryCollector.cpp` | their message receivers are generated for every port; WPE's list lacks the implementations (link errors) |
| `bmalloc/bmalloc/ProcessCheck.h` (2) | inline answers | the Darwin implementations are in `ProcessCheck.mm` |
| `ThirdParty/skia/src/ports/SkMemory_malloc.cpp` (2) | no `<malloc.h>`; `sk_malloc_size` returns the requested size | Skia's Unix code is glibc's; the port's libsystem_malloc has no `malloc_usable_size` |
| `WebKit/Shared/unix/AuxiliaryProcessMain.cpp` (2) | helpers watch the UI process with kqueue (`EVFILT_PROC`, `NOTE_EXIT`) and exit with it | a datagram socket gives no EOF when the peer dies, so helpers outlived the UI |
| same file (2) | crash report on fatal signals (signal, pc/lr, symbolised frames) to stderr | no debugger or crash reporter on the image |

## GL readback (the 2026-10-08 finding behind the last rendering edit)

GL mode ran animated and scrolled pages at ~5 frames/s with a CPU core busy. Timed inside the web process
(`scripts/webkit_frame_timing.sh`): compositing and the GPU took a few ms; `SkSurface::readPixels` of the 960x602 frame took
180-270 ms. Static reading of Skia (`SurfaceContext::readPixels`, `GrGLGpu::onReadPixels`, `GrConvertPixels`) and Mesa
(`st_ReadPixels`, `vc4_formats.c`), confirmed by `tests/pi/gl_readback_bench.c`:

* VC4 renders `GL_RGBA8` as `R8G8B8A8`; WebKit's bitmap is BGRA with no colour space. GLES reads BGRA only if the driver's
  implementation read format says so, so Skia reads RGBA into a temporary buffer and converts on the CPU (also forced by
  `SkColorSpace::Equals(sRGB, null)` being false).
* That temporary is a fresh, zero-filled 2.3 MB allocation per frame (`std::make_unique<char[]>`). On the Pi a fresh allocation
  that size costs ~100 ms in page faults: more than the read itself (~65 ms, bound by ~30 MB/s reads from the write-combined
  buffer mapping, `xnu-iokit-pi3 docs/framebuffer-mapping.md`). The swizzle is ~2 ms.

The edit reads with `glReadPixels(GL_BGRA)` directly into the bitmap (shared-memory targets are not mirrored, so the rows are
top-down as Skia's were) and tells Skia its framebuffer and pixel-store state changed. Measured on the Pi: readback 212 -> 79 ms
and 4.4 -> 12.6 frames/s on reddit.com, 185 -> 74 ms and 5 -> 10 frames/s on an animated test page (10 is the frame timer's cap),
web-process CPU down by more than half on reddit.com.

Not done yet: reading only the damaged area. The read scales with area (100x100 costs ~1.5 ms) and most frames damage a small
part of the page, so this is the next large gain; it needs each swap-chain target's accumulated damage, and a reused buffer (never a
fresh allocation per frame, for the reason above). Also worth raising in the iokit port: ~0.7 ms per 16 KB page to fault in fresh
memory slows every program that allocates.
