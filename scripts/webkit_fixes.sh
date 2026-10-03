#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel)
# The edits Florence needs in the pinned WebKit tree (third_party/webkit) so that the option set in
# config/webkit-options.cmake compiles. Run by scripts/build_webkit.sh; idempotent; each edit asserts
# that the text it changes is exactly what WPE 2.54.0 has (or already carries the edit), so a different
# WebKit makes the build stop here instead of building something else.
#
# The first two are missing preprocessor guards: upstream's builds always have VIDEO and LIBDRM on, and the
# configuration without them does not compile. The third is a feature: WPE forces GL compositing in the
# web process (WebPreferencesWPE.cpp), so "no GPU" would mean software GL (Mesa) and an EGL stack the
# target does not have; GTK has a switch for the non-composited renderer that paints with Skia on the
# CPU into shared memory, WPE has none. All found by building and running it (docs/webkit-port.md). The
# Darwin port will need many more edits, and at that point they move into a fork of WebKit that
# setup_third_party.sh points at, and this file goes away.
set -euo pipefail
WK="${WEBKIT_DIR:-$(cd "$(dirname "$0")/.." && pwd -P)/third_party/webkit}"
[ -f "$WK/Source/cmake/WebKitFeatures.cmake" ] || { echo "error: $WK is not a WebKit checkout" >&2; exit 1; }

python3 - "$WK" <<'PY'
import sys
wk = sys.argv[1]

def edit(path, old, new, marker):
    p = wk + "/" + path
    s = open(p).read()
    if marker in s:
        print("= " + path)
        return
    if s.count(old) != 1:
        sys.exit("error: %s is not the WPE 2.54.0 this fix expects (anchor found %d times)" % (path, s.count(old)))
    open(p, "w").write(s.replace(old, new))
    print("+ " + path)

# ENABLE_VIDEO=OFF: JSHTMLMediaElement is not generated, but its hand-written half is compiled anyway.
edit("Source/WebCore/bindings/js/JSHTMLMediaElementCustom.cpp",
     '#include "config.h"\n#include "JSHTMLMediaElement.h"',
     '#include "config.h"\n\n#if ENABLE(VIDEO) // Florence: this file was compiled with VIDEO off, and then failed\n\n#include "JSHTMLMediaElement.h"',
     "Florence: this file was compiled")
edit("Source/WebCore/bindings/js/JSHTMLMediaElementCustom.cpp",
     "DEFINE_VISIT_ADDITIONAL_CHILDREN_IN_GC_THREAD(JSHTMLMediaElement);\n\n} // namespace WebCore\n",
     "DEFINE_VISIT_ADDITIONAL_CHILDREN_IN_GC_THREAD(JSHTMLMediaElement);\n\n} // namespace WebCore\n\n#endif // ENABLE(VIDEO)\n",
     "#endif // ENABLE(VIDEO)")

# USE_LIBDRM=OFF: DRM_FORMAT_XRGB8888 comes from <drm_fourcc.h>, which is only included with USE(LIBDRM).
edit("Source/WebKit/UIProcess/wpe/AcceleratedBackingStore.cpp",
     "        if (wpe_buffer_dma_buf_get_format(dmaBuffer) == DRM_FORMAT_XRGB8888)\n            alphaType = kOpaque_SkAlphaType;\n",
     "#if USE(LIBDRM)\n        if (wpe_buffer_dma_buf_get_format(dmaBuffer) == DRM_FORMAT_XRGB8888)\n            alphaType = kOpaque_SkAlphaType;\n#else\n        UNUSED_VARIABLE(dmaBuffer);\n#endif\n",
     "UNUSED_VARIABLE(dmaBuffer)")

# WPE has no way out of GL compositing: give it GTK's (HardwareAccelerationManager.cpp:45 reads the same
# variable): WEBKIT_DISABLE_COMPOSITING_MODE=1 selects NonCompositedFrameRenderer, which paints on the CPU.
edit("Source/WebKit/UIProcess/wpe/WebPreferencesWPE.cpp",
     """void WebPreferences::platformInitializeStore()
{
    setAcceleratedCompositingEnabled(true);
    setForceCompositingMode(true);
    setThreadedScrollingEnabled(true);
}""",
     """void WebPreferences::platformInitializeStore()
{
    // Florence: WEBKIT_DISABLE_COMPOSITING_MODE (as in the GTK port) turns GL compositing off, so the web
    // process paints with Skia on the CPU into shared memory and needs no GL stack at all.
    const char* disableCompositing = getenv("WEBKIT_DISABLE_COMPOSITING_MODE");
    bool composited = !(disableCompositing && strcmp(disableCompositing, "0"));
    setAcceleratedCompositingEnabled(composited);
    setForceCompositingMode(composited);
    setThreadedScrollingEnabled(composited);
    setHardwareAccelerationEnabled(composited);     // without this the shared-memory render target still makes GL objects
}""",
     "Florence: WEBKIT_DISABLE_COMPOSITING_MODE")
edit("Source/WebKit/UIProcess/wpe/WebPreferencesWPE.cpp",
     '#include "config.h"\n#include "WebPreferences.h"\n',
     '#include "config.h"\n#include "WebPreferences.h"\n\n#include <stdlib.h>\n#include <string.h>\n',
     "#include <stdlib.h>")
PY
