# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
#
# WebKit build options for Florence: WPE WebKit 2.54.0, software rendering, as little of it as
# will build. Passed to CMake as an initial cache (`cmake -C config/webkit-options.cmake ...`) by
# scripts/build_webkit.sh, which first runs scripts/check_webkit_options.sh to prove that every
# name below still exists in the pinned tree.
#
# The target is a Raspberry Pi 3 (1 GB, four slow cores, no GPU driver). The rule for this file:
# a feature is OFF unless a web page that works without it would stop working, or turning it off
# does not build. Every OFF falls in one of four groups, and each group says what it saves:
#   deps    a library (and its cross-build) that does not have to exist at all
#   procs   a process, thread or timer that would otherwise run while the browser is idle
#   code    code that is never run and only costs RAM (binary size, page-in) and build time
#   risk    could not be turned off or is deliberately left on: see "Left on" at the bottom
#
# Things that cannot be turned off, stated once so nobody hunts for the switch:
#   * JavaScriptCore itself. WebKit has no "no JavaScript" build. JavaScript is instead off at
#     run time (frontend/flo_engine.c sets enable-javascript FALSE until the user opts in), and
#     everything that makes it fast (JIT, WebAssembly) is off here, so the interpreter ("CLoop")
#     is all there is and its code pages are never touched while JS is off.
#   * libepoxy, libgcrypt, libtasn1, libxkbcommon, WebP: Source/cmake/OptionsWPE.cmake
#     find_package(... REQUIRED)s them unconditionally even with every feature that uses them
#     off. Each is small. Making them optional is a patch for the anurodhp/WebKit fork, not a flag.
#   * The multi-process model. UI process (our app) + one WebProcess + one NetworkProcess is how
#     WPE works; the processes are started with fork/exec, which suits this port (see CLAUDE.md,
#     "no threads beside GNUstep's run loop"), but it costs RAM.
# Runtime knobs (software painting, a 30 fps frame cap, one painting thread) are
# environment variables set by frontend/flo_engine.c, not build options.

macro(flo_set name value)
    set(${name} ${value} CACHE BOOL "Florence" FORCE)
endmacro()

# ---- the engine: the interpreter only, the system allocator ----------------------------------
# procs/code: no JIT tiers (also no executable-memory entitlement dance on Darwin), no WebAssembly
# (needs a JIT tier), no sampling profiler thread.
# FL_WK_JIT=1 in the environment of scripts/build_webkit.sh: the baseline and DFG JIT tiers (docs/jit-plan.md; tests/pi/jit_probe.c shows
# this kernel runs MAP_JIT and mprotect-to-RX code). FTL needs B3/LLVM and a lot of RAM: never.
if ("$ENV{FL_WK_JIT}" STREQUAL "1")
    flo_set(ENABLE_JIT ON)
    flo_set(ENABLE_DFG_JIT ON)
else ()
    flo_set(ENABLE_JIT OFF)
    flo_set(ENABLE_DFG_JIT OFF)
endif ()
flo_set(ENABLE_FTL_JIT OFF)
# LLInt, JavaScriptCore's assembly interpreter (offlineasm, no JIT, no executable memory): several times faster than the C
# "CLoop" interpreter this was before (ENABLE_C_LOOP ON), for the same memory.
flo_set(ENABLE_C_LOOP OFF)
flo_set(ENABLE_WEBASSEMBLY OFF)
flo_set(ENABLE_WEBASSEMBLY_BBQJIT OFF)
flo_set(ENABLE_WEBASSEMBLY_OMGJIT OFF)
flo_set(ENABLE_SAMPLING_PROFILER OFF)
# The system allocator: bmalloc/libpas keeps large caches for speed; the iokit port's libmalloc
# is the one allocator already known to work (and to return memory).
flo_set(USE_SYSTEM_MALLOC ON)
flo_set(USE_MIMALLOC OFF)
flo_set(USE_ISO_MALLOC OFF)

# ---- graphics: Skia on the CPU, no GPU, no GL (a GPU port is later work) ---------------------
# deps/procs: no GPU process, no GBM/DRM, no WebGL (and with it ANGLE), no WebGPU, no Vulkan.
flo_set(USE_SKIA ON)
flo_set(ENABLE_GPU_PROCESS OFF)
flo_set(USE_GBM OFF)
flo_set(USE_LIBDRM OFF)
flo_set(ENABLE_WEBGL OFF)
flo_set(ENABLE_WEBGPU OFF)
flo_set(USE_VULKAN OFF)
flo_set(ENABLE_OFFSCREEN_CANVAS OFF)
flo_set(ENABLE_OFFSCREEN_CANVAS_IN_WORKERS OFF)

# ---- the WPE port: our own platform (frontend/flo_platform.c), none of the stock ones --------
# deps: no libwpe, no wayland, no libinput/udev, no DRM, no Qt, no Cog, no GObject introspection
# or gi-docgen. WPEPlatform (the new embedder API) is what the GNUstep UI plugs a display into.
flo_set(ENABLE_WPE_PLATFORM ON)
flo_set(ENABLE_WPE_LEGACY_API OFF)
flo_set(ENABLE_WPE_PLATFORM_DRM OFF)
flo_set(ENABLE_WPE_PLATFORM_WAYLAND OFF)
flo_set(ENABLE_WPE_PLATFORM_HEADLESS OFF)
flo_set(ENABLE_WPE_QT_API OFF)
flo_set(ENABLE_WPE_1_1_API OFF)
flo_set(ENABLE_COG OFF)
flo_set(ENABLE_INTROSPECTION OFF)
flo_set(ENABLE_DOCUMENTATION OFF)
flo_set(USE_ATK OFF)

# ---- media: none --------------------------------------------------------------------------
# deps: GStreamer is not pulled in at all (the biggest single dependency avoided). A page with a
# <video> gets the "cannot play" fallback instead of a decoder.
flo_set(USE_GSTREAMER OFF)            # without this CMake still insists on finding GStreamer, even with every feature below off
flo_set(USE_GSTREAMER_GL OFF)
flo_set(USE_GSTREAMER_WEBRTC OFF)
flo_set(ENABLE_VIDEO OFF)
flo_set(ENABLE_WEB_AUDIO OFF)
flo_set(ENABLE_MEDIA_SOURCE OFF)
flo_set(ENABLE_MEDIA_SOURCE_IN_WORKERS OFF)
flo_set(ENABLE_MEDIA_STREAM OFF)
flo_set(ENABLE_MEDIA_CAPTURE OFF)
flo_set(ENABLE_MEDIA_RECORDER OFF)
flo_set(ENABLE_MEDIA_SESSION OFF)
flo_set(ENABLE_MEDIA_SESSION_PLAYLIST OFF)
flo_set(ENABLE_MEDIA_CONTROLS_CONTEXT_MENUS OFF)
flo_set(ENABLE_VIDEO_USES_ELEMENT_FULLSCREEN OFF)
flo_set(ENABLE_ENCRYPTED_MEDIA OFF)
flo_set(ENABLE_THUNDER OFF)
flo_set(ENABLE_WEB_CODECS OFF)
flo_set(ENABLE_WEB_RTC OFF)
flo_set(ENABLE_PICTURE_IN_PICTURE_API OFF)
flo_set(ENABLE_SPEECH_SYNTHESIS OFF)
flo_set(USE_FLITE OFF)

# ---- images and fonts: the formats the web actually uses ------------------------------------
# deps: no AVIF, JPEG XL or colour management. JPEG, PNG, GIF, WebP, SVG and WOFF2 stay.
flo_set(USE_AVIF OFF)
flo_set(USE_JPEGXL OFF)
flo_set(USE_LCMS OFF)
flo_set(USE_LIBHYPHEN OFF)

# ---- document features nobody needs on a 1 GB machine ----------------------------------------
# deps: libxslt, Enchant (spell checking) and PDF.js are not built. code: the rest.
flo_set(ENABLE_XSLT OFF)
flo_set(ENABLE_SPELLCHECK OFF)
flo_set(ENABLE_PDFJS OFF)
flo_set(ENABLE_MATHML OFF)
flo_set(ENABLE_MHTML OFF)
flo_set(ENABLE_ATTACHMENT_ELEMENT OFF)
flo_set(ENABLE_APPLICATION_MANIFEST OFF)
flo_set(ENABLE_MODEL_ELEMENT OFF)
flo_set(ENABLE_TELEPHONE_NUMBER_DETECTION OFF)
flo_set(ENABLE_PAYMENT_REQUEST OFF)
flo_set(ENABLE_WEB_AUTHN OFF)
flo_set(ENABLE_NOTIFICATIONS OFF)
flo_set(ENABLE_GEOLOCATION OFF)
flo_set(ENABLE_GAMEPAD OFF)
flo_set(ENABLE_DEVICE_ORIENTATION OFF)
flo_set(ENABLE_ORIENTATION_EVENTS OFF)
flo_set(ENABLE_WEBXR OFF)
flo_set(ENABLE_WEBXR_HIT_TEST OFF)
flo_set(ENABLE_WEBXR_LAYERS OFF)
flo_set(ENABLE_WK_WEB_EXTENSIONS OFF)
flo_set(ENABLE_WEB_API_STATISTICS OFF)

# ---- a desktop mouse-and-keyboard browser, not a phone or a kiosk ----------------------------
# procs: smooth scrolling is an animation timer that fires every frame while a page scrolls.
flo_set(ENABLE_TOUCH_EVENTS OFF)
flo_set(ENABLE_SMOOTH_SCROLLING OFF)
flo_set(ENABLE_FULLSCREEN_API OFF)
flo_set(ENABLE_POINTER_LOCK OFF)
flo_set(ENABLE_CURSOR_VISIBILITY OFF)
flo_set(ENABLE_MOUSE_CURSOR_SCALE OFF)
flo_set(ENABLE_CSS_TAP_HIGHLIGHT_COLOR OFF)
flo_set(ENABLE_AUTOCAPITALIZE OFF)
flo_set(ENABLE_DARK_MODE_CSS OFF)

# ---- diagnostics, tooling, tests -------------------------------------------------------------
# procs/deps: no remote inspector (a listening socket and a thread), no WebDriver, no MiniBrowser,
# no jsc shell, no journald, no libbacktrace, no sysprof, no release log, no bubblewrap sandbox
# (Linux only; there is no user-namespace sandbox on this port, which is stated, not hidden).
# (DEVELOPER_MODE is not an option here: it is unset, which is off, and must stay unset: it pulls in
#  MiniBrowser, the API tests, Thunder and more.)
flo_set(ENABLE_REMOTE_INSPECTOR OFF)
flo_set(ENABLE_WEBDRIVER OFF)
flo_set(ENABLE_WEBDRIVER_BIDI OFF)
flo_set(ENABLE_MINIBROWSER OFF)
flo_set(ENABLE_JAVASCRIPT_SHELL OFF)
flo_set(ENABLE_API_TESTS OFF)
flo_set(ENABLE_LAYOUT_TESTS OFF)
flo_set(ENABLE_IMAGE_DIFF OFF)
flo_set(ENABLE_JOURNALD_LOG OFF)
flo_set(USE_LIBBACKTRACE OFF)
flo_set(USE_SYSPROF_CAPTURE OFF)
flo_set(USE_SYSTEM_SYSPROF_CAPTURE OFF)
flo_set(ENABLE_RELEASE_LOG OFF)
flo_set(ENABLE_BUBBLEWRAP_SANDBOX OFF)
flo_set(ENABLE_MEMORY_SAMPLER OFF)
flo_set(ENABLE_RESOURCE_USAGE OFF)
flo_set(ENABLE_BREAKPAD OFF)

# ---- Left on, deliberately -------------------------------------------------------------------
# ENABLE_CONTENT_EXTENSIONS   Safari content-blocker rule lists, native in WebKit: it is how
#                             Florence blocks ads (frontend/assets/blocklist-default.json is
#                             already in that format). Blocking costs less than it saves.
# ENABLE_DRAG_SUPPORT         the option that also gives selecting text with the mouse.
# ENABLE_CONTEXT_MENUS        the WebKit API the UI will build its menu from.
# ENABLE_SHAREABLE_RESOURCE   network -> web process resource hand-off without a copy (saves RAM).
# ENABLE_PERIODIC_MEMORY_MONITOR  the only memory-pressure signal on a port with no swap and no
#                             kernel notifications; it is what frees caches when RAM runs low.
# ENABLE_ASYNC_SCROLLING      and ENABLE_VARIATION_FONTS: both change how pages scroll or look, so
#                             they stay until measured on the Pi. (Async scrolling cannot be
#                             switched off at run time: WEBKIT_DISABLE_ASYNC_SCROLLING is only read
#                             by developer-mode builds, DrawingAreaCoordinatedGraphics.cpp:212.)
# ENABLE_UNIFIED_BUILDS       build speed only; has no runtime effect.
