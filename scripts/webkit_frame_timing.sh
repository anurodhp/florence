#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
# DIAGNOSTIC, not part of the build: per-frame timing in the web process, to stderr (the engine's log) when FLORENCE_READBACK_LOG is set:
#   frame: flush <ms> paint <ms> swap <ms> present <ms>   (ThreadedCompositor::renderLayerTree phases; present = didRenderFrame, i.e. the readback)
#     skia: flush+wait <ms> readPixels <ms>               (the two halves of the SHM readback)
# Apply, build (ninja -C build/webkit-cross-jit WebKit), deploy the library beside the installed one; undo with
#   git -C third_party/webkit checkout Source/WebKit/WebProcess/WebPage/CoordinatedGraphics/ThreadedCompositor.cpp \
#                                      Source/WebKit/WebProcess/WebPage/CoordinatedGraphics/AcceleratedSurface.cpp
set -euo pipefail
WK="${WEBKIT_DIR:-$(cd "$(dirname "$0")/.." && pwd -P)/third_party/webkit}"
python3 - "$WK" <<'PY'
import sys
wk = sys.argv[1]
def edit(path, old, new, marker):
    p = wk + "/" + path
    s = open(p).read()
    if marker in s:
        print("= " + path); return
    if s.count(old) != 1:
        sys.exit("error: %s: anchor found %d times" % (path, s.count(old)))
    open(p, "w").write(s.replace(old, new)); print("+ " + path)
D = "Source/WebKit/WebProcess/WebPage/CoordinatedGraphics/"
edit(D + "ThreadedCompositor.cpp",
     "    m_surface->willRenderFrame(viewportSize);\n\n    RunLoop::mainSingleton().dispatch([this, protectedThis = Ref { *this }] {\n        if (m_layerTreeHost)\n            m_layerTreeHost->willRenderFrame();",
     "    const auto florenceT0 = MonotonicTime::now(); // Florence timing\n    m_surface->willRenderFrame(viewportSize);\n\n    RunLoop::mainSingleton().dispatch([this, protectedThis = Ref { *this }] {\n        if (m_layerTreeHost)\n            m_layerTreeHost->willRenderFrame();",
     "florenceT0")
edit(D + "ThreadedCompositor.cpp",
     "    WTFEndSignpost(this, FlushCompositingState);\n",
     "    WTFEndSignpost(this, FlushCompositingState);\n    const auto florenceT1 = MonotonicTime::now();\n",
     "florenceT1")
edit(D + "ThreadedCompositor.cpp",
     "    WTFEndSignpost(this, PaintToGLContext);\n",
     "    WTFEndSignpost(this, PaintToGLContext);\n    const auto florenceT2 = MonotonicTime::now();\n",
     "florenceT2")
edit(D + "ThreadedCompositor.cpp",
     "    m_surface->didRenderFrame(targetContents);\n    m_surface->sendFrame();\n",
     "    const auto florenceT3 = MonotonicTime::now();\n    m_surface->didRenderFrame(targetContents);\n    const auto florenceT4 = MonotonicTime::now();\n    if (getenv(\"FLORENCE_READBACK_LOG\"))\n        fprintf(stderr, \"frame: flush %.1f paint %.1f swap %.1f present %.1f\\n\", (florenceT1 - florenceT0).milliseconds(), (florenceT2 - florenceT1).milliseconds(), (florenceT3 - florenceT2).milliseconds(), (florenceT4 - florenceT3).milliseconds());\n    m_surface->sendFrame();\n",
     "florenceT4")
edit(D + "AcceleratedSurface.cpp",
     "            m_skiaSurface->readPixels(info, m_bitmap->mutableSpan().data(), m_bitmap->bytesPerRow(), 0, 0);\n        } else",
     "            auto florenceF0 = MonotonicTime::now(); // Florence timing\n            PlatformDisplay::sharedDisplay().skiaGrContext()->flushAndSubmit(GrSyncCpu::kYes);\n            auto florenceF1 = MonotonicTime::now();\n            m_skiaSurface->readPixels(info, m_bitmap->mutableSpan().data(), m_bitmap->bytesPerRow(), 0, 0);\n            if (getenv(\"FLORENCE_READBACK_LOG\"))\n                fprintf(stderr, \"  skia: flush+wait %.1f readPixels %.1f\\n\", (florenceF1 - florenceF0).milliseconds(), (MonotonicTime::now() - florenceF1).milliseconds());\n        } else",
     "florenceF1")
edit(D + "AcceleratedSurface.cpp",
     "#include <skia/gpu/ganesh/GrBackendSurface.h>\n",
     "#include <skia/gpu/ganesh/GrBackendSurface.h>\n#include <skia/gpu/ganesh/GrDirectContext.h> // Florence timing\n",
     "GrDirectContext.h> // Florence timing")
PY
