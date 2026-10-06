#!/bin/bash
# SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
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
# Darwin edits are the section after the first four; the Darwin port will need many more edits, and at that point they move into a fork of WebKit that
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

# ---- Darwin (this port compiled for the iokit port's kernel) ---------------------------------------------------
# build_webkit.sh clears CMake's APPLE after project() so the toolkit-less port takes its non-Cocoa branches; those
# branches name Linux-only sources. Replace them by what Darwin has (WTF's own Cocoa-free ones).
edit("Source/WTF/wtf/PlatformWPE.cmake",
     "    linux/CurrentProcessMemoryStatus.cpp\n    linux/RealTimeThreads.cpp\n\n    posix/CPUTimePOSIX.cpp",
     "    posix/CPUTimePOSIX.cpp",
     "Florence: Darwin picks")
edit("Source/WTF/wtf/PlatformWPE.cmake",
     "else ()\n    list(APPEND WTF_SOURCES\n        linux/MemoryFootprintLinux.cpp\n\n        unix/MemoryPressureHandlerUnix.cpp\n    )\nendif ()",
     "elseif (CMAKE_SYSTEM_NAME STREQUAL \"Darwin\") # Florence: Darwin picks task_info for the footprint, no pressure source yet\n    list(APPEND WTF_SOURCES\n        cocoa/MemoryFootprintCocoa.cpp\n        generic/MemoryPressureHandlerGeneric.cpp\n    )\nelse ()\n    list(APPEND WTF_SOURCES\n        linux/CurrentProcessMemoryStatus.cpp\n        linux/RealTimeThreads.cpp\n        linux/MemoryFootprintLinux.cpp\n\n        unix/MemoryPressureHandlerUnix.cpp\n    )\nendif ()",
     "Florence: Darwin picks task_info")

# OS(DARWIN) code that the GTK port opts out of with !PLATFORM(GTK) (it is the one non-Cocoa port that builds on Darwin,
# via MacPorts); WPE takes the same exits: WebCrypto through gcrypt, not CommonCrypto; POSIX signals, not Mach
# exceptions; no Accelerate framework.
for path, old, new, marker in [
    ("Source/WebCore/crypto/keys/CryptoKeyEC.h", "#if OS(DARWIN) && !PLATFORM(GTK)", "#if OS(DARWIN) && !PLATFORM(GTK) && !PLATFORM(WPE)", "!PLATFORM(WPE)"),
    ("Source/WebCore/crypto/keys/CryptoKeyEC.cpp", "#if OS(DARWIN) && !PLATFORM(GTK)", "#if OS(DARWIN) && !PLATFORM(GTK) && !PLATFORM(WPE)", "!PLATFORM(WPE)"),
    ("Source/WebCore/crypto/keys/CryptoKeyRSA.h", "#if OS(DARWIN) && !PLATFORM(GTK)", "#if OS(DARWIN) && !PLATFORM(GTK) && !PLATFORM(WPE)", "!PLATFORM(WPE)"),
    ("Source/WebCore/crypto/CryptoKey.cpp", "#if !OS(DARWIN) || PLATFORM(GTK)", "#if !OS(DARWIN) || PLATFORM(GTK) || PLATFORM(WPE)", "|| PLATFORM(WPE)"),
    ("Source/WTF/wtf/PlatformHave.h", "<mach/mach_exc.defs>) && !PLATFORM(GTK)", "<mach/mach_exc.defs>) && !PLATFORM(GTK) && !PLATFORM(WPE)", "!PLATFORM(WPE)"),
    ("Source/WTF/wtf/PlatformUse.h", "#if OS(DARWIN) && !PLATFORM(GTK)\n#define USE_ACCELERATE 1", "#if OS(DARWIN) && !PLATFORM(GTK) && !PLATFORM(WPE)\n#define USE_ACCELERATE 1", "!PLATFORM(WPE)"),
]:
    edit(path, old, new, marker)

# IPC on Darwin without Mach ports or XPC: the unix-domain-socket transport (USE(UNIX_DOMAIN_SOCKETS), which WPE has)
# must win over OS(DARWIN) wherever both are true. The MacPorts WebKitGTK patch set (patch-webkit-ipc-darwin-gtk,
# patch-webkit-serialization-elif-darwin, patch-ipc-darwin-sock-dgram) is the model; the 2.54 tree already orders most
# of these chains USE(UNIX_DOMAIN_SOCKETS)-first, these are the ones it does not.
edit("Source/WebKit/Platform/IPC/Attachment.h",
     "#if OS(DARWIN)\n#include <wtf/MachSendRight.h>\n#endif",
     "#if OS(DARWIN) && !USE(UNIX_DOMAIN_SOCKETS) // Florence: sockets win on Darwin\n#include <wtf/MachSendRight.h>\n#endif",
     "Florence: sockets win on Darwin")
edit("Source/WebKit/Platform/IPC/Attachment.h",
     "#if OS(DARWIN)\nusing Attachment = MachSendRight;",
     "#if USE(UNIX_DOMAIN_SOCKETS)\nusing Attachment = UnixFileDescriptor;\n#elif OS(DARWIN)\nusing Attachment = MachSendRight;",
     "using Attachment = UnixFileDescriptor;\n#elif OS(DARWIN)")
edit("Source/WebKit/Platform/IPC/ConnectionHandle.serialization.in",
     "#if OS(DARWIN)\n    MachSendRight m_handle;",
     "#if OS(DARWIN) && !USE(UNIX_DOMAIN_SOCKETS)\n    MachSendRight m_handle;",
     "OS(DARWIN) && !USE(UNIX_DOMAIN_SOCKETS)")
edit("Source/WebKit/Shared/WebCoreArgumentCoders.serialization.in",
     "#if OS(DARWIN)\nheader: <wtf/MachSendRight.h>",
     "#if OS(DARWIN) && !USE(UNIX_DOMAIN_SOCKETS)\nheader: <wtf/MachSendRight.h>",
     "#if OS(DARWIN) && !USE(UNIX_DOMAIN_SOCKETS)\nheader: <wtf/MachSendRight.h>")
edit("Source/WebKit/Shared/WebCoreArgumentCoders.serialization.in",
     "#if OS(DARWIN)\n    [Validator='!!m_handle && *m_handle'] MachSendRight m_handle;",
     "#if OS(DARWIN) && !USE(UNIX_DOMAIN_SOCKETS)\n    [Validator='!!m_handle && *m_handle'] MachSendRight m_handle;",
     "#if OS(DARWIN) && !USE(UNIX_DOMAIN_SOCKETS)\n    [Validator")
edit("Source/WebKit/Platform/IPC/Connection.h",
     "#if OS(DARWIN)\n    xpc_connection_t xpcConnection() const { return m_xpcConnection.get(); }\n",
     "#if OS(DARWIN)\n#if !USE(UNIX_DOMAIN_SOCKETS) // Florence: m_xpcConnection only exists for the Mach transport\n    xpc_connection_t xpcConnection() const { return m_xpcConnection.get(); }\n#endif\n",
     "only exists for the Mach transport")
edit("Source/WebKit/Platform/IPC/glib/ConnectionGLib.cpp",
     "pid_t Connection::remoteProcessID(GSocket* socket)",
     "#if OS(DARWIN)\n// Florence: OS(DARWIN) code in the UI/network/web process logs the peer's pid and passes its audit token; with a plain\n// socket there is neither (no Mach, no audit token), as in the MacPorts WebKitGTK patch.\npid_t Connection::remoteProcessID() const\n{\n    return 0;\n}\n\nstd::optional<audit_token_t> Connection::getAuditToken()\n{\n    return std::nullopt;\n}\n#endif\n\npid_t Connection::remoteProcessID(GSocket* socket)",
     "return std::nullopt;")
# macOS' AF_UNIX datagram sockets (the only kind WebKit uses here, SOCK_SEQPACKET does not exist) have 2 KB send / 4 KB
# receive buffers by default (xnu-7195 bsd/kern/uipc_usrreq.c:865-866); IPC messages go up to 4 KB.
edit("Source/WebKit/Platform/IPC/unix/IPCUtilitiesUnix.cpp",
     "    std::array<int, 2> sockets;\n\n#if OS(LINUX)",
     "    std::array<int, 2> sockets;\n\n#if OS(DARWIN)\n    // Florence: datagram sockets with enlarged buffers (default 2 KB / 4 KB on xnu, messages go up to 4 KB)\n    socketType = SOCK_DGRAM;\n    RELEASE_ASSERT(socketpair(AF_UNIX, socketType, 0, sockets.data()) != -1);\n    constexpr int bufferSize = 262144;\n    for (int fd : sockets) {\n        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufferSize, sizeof(bufferSize));\n        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufferSize, sizeof(bufferSize));\n    }\n    if (options & SetCloexecOnServer)\n        RELEASE_ASSERT(setCloseOnExec(sockets[1]));\n    if (options & SetCloexecOnClient)\n        RELEASE_ASSERT(setCloseOnExec(sockets[0]));\n    return { { sockets[0], UnixFileDescriptor::Adopt }, { sockets[1], UnixFileDescriptor::Adopt } };\n#endif\n\n#if OS(LINUX)",
     "Florence: datagram sockets with enlarged buffers")
edit("Source/WebKit/Platform/IPC/Connection.h",
     "#if OS(DARWIN)\n#include <mach/mach_port.h>\n#include <wtf/darwin/DispatchOSObject.h>",
     "#if OS(DARWIN) && !USE(UNIX_DOMAIN_SOCKETS) // Florence: dispatch/XPC objects only for the Mach transport\n#include <mach/mach_port.h>\n#include <wtf/darwin/DispatchOSObject.h>",
     "dispatch/XPC objects only for the Mach transport")
edit("Source/WebKit/Platform/IPC/Connection.h",
     "#endif // OS(DARWIN)\n\n#if USE(GLIB)\n#include <wtf/glib/GSocketMonitor.h>",
     "#endif // OS(DARWIN)\n\n#if OS(DARWIN) && USE(UNIX_DOMAIN_SOCKETS)\n#include <mach/message.h> // audit_token_t, for the getAuditToken() stub\n#endif\n\n#if USE(GLIB)\n#include <wtf/glib/GSocketMonitor.h>",
     "audit_token_t, for the getAuditToken() stub")

# Code that only compiles with a newer GLib / with Apple's headers in the other mode than ours:
# g_task_return_new_error_literal is GLib 2.80 (WebKit's MHTML-off branch uses it; we ship 2.78, WebKit asks for 2.70).
edit("Source/WebKit/UIProcess/API/glib/WebKitWebView.cpp",
     '#include "APIContentWorld.h"\n#include "APIData.h"',
     '#include <gio/gio.h>\n#if !GLIB_CHECK_VERSION(2, 80, 0) // Florence: GLib 2.78 has no g_task_return_new_error_literal\n#define g_task_return_new_error_literal(task, domain, code, message) g_task_return_error(task, g_error_new_literal(domain, code, message))\n#endif\n\n#include "APIContentWorld.h"\n#include "APIData.h"',
     "GLib 2.78 has no g_task_return_new_error_literal")
# WKPagePrivate.h declares the key-event function under !defined(__APPLE__), WKPage.cpp defines it under !PLATFORM(COCOA);
# this port is both __APPLE__ and not COCOA.
edit("Source/WebKit/UIProcess/API/C/WKPage.cpp",
     "#if !PLATFORM(COCOA)\nvoid WKPageDoAfterProcessingAllPendingKeyEvents(",
     "#if !PLATFORM(COCOA) && !defined(__APPLE__) // Florence: matches the declaration in WKPagePrivate.h\nvoid WKPageDoAfterProcessingAllPendingKeyEvents(",
     "matches the declaration in WKPagePrivate.h")
# TARGET_OS_IPHONE is 1 because the SDK is the iPhoneOS one; this is not an iOS port.
edit("Source/WebKit/WebProcess/InjectedBundle/API/c/WKBundlePage.cpp",
     "#if TARGET_OS_IPHONE\nvoid WKBundlePageSetUseTestingViewportConfiguration(",
     "#if TARGET_OS_IPHONE && PLATFORM(IOS_FAMILY) // Florence: the iPhoneOS SDK sets TARGET_OS_IPHONE for this port too\nvoid WKBundlePageSetUseTestingViewportConfiguration(",
     "the iPhoneOS SDK sets TARGET_OS_IPHONE")

# Missing pieces of the Darwin link (first libWPEWebKit link: 39 undefined symbols, the ones not in libflocompat):
# the gesture controller's message receivers are generated for every port, GTK compiles the implementations; the
# WPE source lists lack them.
edit("Source/WebKit/SourcesWPE.txt", "UIProcess/ViewSnapshotStore.cpp\n", "UIProcess/ViewGestureController.cpp\nUIProcess/ViewSnapshotStore.cpp\n", "UIProcess/ViewGestureController.cpp")
edit("Source/WebKit/SourcesWPE.txt", "WebProcess/WebPage/CoordinatedGraphics/AcceleratedSurface.cpp\n", "WebProcess/WebPage/ViewGestureGeometryCollector.cpp\nWebProcess/WebPage/CoordinatedGraphics/AcceleratedSurface.cpp\n", "WebProcess/WebPage/ViewGestureGeometryCollector.cpp")
# JSC::Options (OS(DARWIN)) logs through WTF::OSLogPrintStream, whose only implementation is Objective-C++ (OSLogPrintStream.mm,
# Cocoa); this one writes to stderr. bmalloc's Darwin ProcessCheck functions live in ProcessCheck.mm: inline answers.
import os
stub = wk + "/Source/WTF/wtf/darwin/OSLogPrintStream.cpp"
if not os.path.exists(stub):
    open(stub, "w").write("""// Florence: stand-in for OSLogPrintStream.mm (Objective-C++, Cocoa); prints to stderr instead of os_log.
#include "config.h"
#include <wtf/darwin/OSLogPrintStream.h>

#if OS(DARWIN)
#include <cstdio>

namespace WTF {

OSLogPrintStream::OSLogPrintStream(os_log_t log, os_log_type_t type)
    : m_log(log)
    , m_logType(type)
{
}

OSLogPrintStream::~OSLogPrintStream() = default;

std::unique_ptr<OSLogPrintStream> OSLogPrintStream::open(const char*, const char*, os_log_type_t type)
{
    return makeUnique<OSLogPrintStream>(nullptr, type);
}

void OSLogPrintStream::vprintf(const char* format, va_list args)
{
    std::vfprintf(stderr, format, args);
}

} // namespace WTF

#endif // OS(DARWIN)
""")
    print("+ Source/WTF/wtf/darwin/OSLogPrintStream.cpp")
edit("Source/WTF/wtf/PlatformWPE.cmake",
     "        cocoa/MemoryFootprintCocoa.cpp\n        generic/MemoryPressureHandlerGeneric.cpp\n    )",
     "        cocoa/MemoryFootprintCocoa.cpp\n        darwin/OSLogPrintStream.cpp\n        generic/MemoryPressureHandlerGeneric.cpp\n    )",
     "darwin/OSLogPrintStream.cpp")
edit("Source/bmalloc/bmalloc/ProcessCheck.h",
     "#else\nbool gigacageEnabledForProcess();\n#endif",
     "#else\ninline bool gigacageEnabledForProcess() { return false; } // Florence: ProcessCheck.mm is Cocoa; no Gigacage here\n#endif",
     "no Gigacage here")
edit("Source/bmalloc/bmalloc/ProcessCheck.h",
     "const char* processNameString();\n\nbool shouldAllowMiniMode();",
     "inline const char* processNameString() { return \"\"; } // Florence: as the MacPorts WebKitGTK patch\ninline bool shouldAllowMiniMode() { return true; }",
     "as the MacPorts WebKitGTK patch")

# Skia is told this is a Unix (SK_BUILD_FOR_UNIX, build_webkit.sh) so it does not reach for CoreFoundation, but its Unix
# memory code is glibc's: <malloc.h> and malloc_usable_size. The port's libsystem_malloc has neither malloc_size nor
# malloc_usable_size, and the answer "exactly what was asked for" is always valid for sk_malloc_size.
edit("Source/ThirdParty/skia/src/ports/SkMemory_malloc.cpp",
     "#elif defined(SK_BUILD_FOR_ANDROID) || defined(SK_BUILD_FOR_UNIX)\n#include <malloc.h>",
     "#elif defined(SK_BUILD_FOR_ANDROID) || (defined(SK_BUILD_FOR_UNIX) && !defined(__APPLE__)) // Florence: no malloc.h on Darwin\n#include <malloc.h>",
     "Florence: no malloc.h on Darwin")
edit("Source/ThirdParty/skia/src/ports/SkMemory_malloc.cpp",
     "    #elif defined(SK_BUILD_FOR_UNIX)\n        completeSize = malloc_usable_size(addr);",
     "    #elif defined(SK_BUILD_FOR_UNIX) && !defined(__APPLE__) // Florence: Darwin without malloc_size: the requested size\n        completeSize = malloc_usable_size(addr);",
     "Florence: Darwin without malloc_size")

# The web process creates an EGL display at startup (initializePlatformDisplayIfNeeded) and CRASH()es without one, even
# though the non-composited renderer (WEBKIT_DISABLE_COMPOSITING_MODE, above) paints on the CPU into shared memory and never
# needs it. The Pi has no EGL: carry on without a display (the MacPorts WebKitGTK patch, patch-egl-display-no-crash).
edit("Source/WebKit/WebProcess/glib/WebProcessGLib.cpp",
     '    WTFLogAlways("Could not create EGL display: no supported platform available. Aborting...");\n    CRASH();\n}',
     '    // Florence: no EGL display is fine, the CPU renderer does not need one\n    return;\n}',
     "Florence: no EGL display is fine")
# The helper processes outlive the UI process when it dies. Upstream relies on the IPC socket's EOF, but the Darwin IPC here
# is a datagram socketpair (SOCK_SEQPACKET does not exist on xnu-7195), and a datagram socket gives no EOF when the peer
# closes. Watch the parent with kqueue (EVFILT_PROC, NOTE_EXIT) from the helper's own GLib main loop: no timer, no polling.
edit("Source/WebKit/Shared/unix/AuxiliaryProcessMain.cpp",
     "#if USE(GLIB)\n#include <glib-unix.h>\n#endif\n",
     "#if USE(GLIB)\n#include <glib-unix.h>\n#endif\n\n#if OS(DARWIN) && USE(GLIB) // Florence: exit with the UI process\n#include <errno.h>\n#include <sys/event.h>\n#include <unistd.h>\n#endif\n",
     "Florence: exit with the UI process")
edit("Source/WebKit/Shared/unix/AuxiliaryProcessMain.cpp",
     "    RELEASE_ASSERT(!sigaction(SIGPIPE, &signalAction, nullptr));\n#if ENABLE(LLVM_PROFILE_GENERATION) && USE(GLIB)",
     "    RELEASE_ASSERT(!sigaction(SIGPIPE, &signalAction, nullptr));\n#if OS(DARWIN) && USE(GLIB)\n    {\n        // Florence: see scripts/webkit_fixes.sh, the helper exits when the UI process does\n        pid_t parent = getppid();\n        int queue = kqueue();\n        struct kevent change;\n        if (parent <= 1)\n            _exit(0);\n        EV_SET(&change, parent, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);\n        if (queue >= 0 && kevent(queue, &change, 1, nullptr, 0, nullptr) == 0) {\n            g_unix_fd_add(queue, G_IO_IN, [](gint, GIOCondition, gpointer) -> gboolean {\n                _exit(0);\n                return G_SOURCE_REMOVE;\n            }, nullptr);\n        } else if (queue >= 0 && errno == ESRCH)\n            _exit(0); // the parent is already gone\n    }\n#endif\n#if ENABLE(LLVM_PROFILE_GENERATION) && USE(GLIB)",
     "Florence: see scripts/webkit_fixes.sh, the helper exits")
# A helper process that crashes at startup leaves nothing behind (no debugger, no crash reporter on the image, and its stderr is
# the UI's log): print the signal, fault address, pc/lr and a frame-pointer walk with symbols (dladdr) to stderr before dying.
edit("Source/WebKit/Shared/unix/AuxiliaryProcessMain.cpp",
     "AuxiliaryProcessMainCommon::AuxiliaryProcessMainCommon()\n{\n#if ENABLE(BREAKPAD)\n    installBreakpadExceptionHandler();\n#endif\n}",
     "#if OS(DARWIN) // Florence: crash report of a helper process\nstatic void floHelperCrash(int sig, siginfo_t* info, void* context)\n{\n    ucontext_t* uc = static_cast<ucontext_t*>(context);\n    uintptr_t pc = static_cast<uintptr_t>(__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss));\n    uintptr_t lr = static_cast<uintptr_t>(__darwin_arm_thread_state64_get_lr(uc->uc_mcontext->__ss));\n    uintptr_t* fp = reinterpret_cast<uintptr_t*>(__darwin_arm_thread_state64_get_fp(uc->uc_mcontext->__ss));\n    auto describe = [](int n, uintptr_t address) {\n        Dl_info di { };\n        if (dladdr(reinterpret_cast<void*>(address), &di) && di.dli_fname)\n            dprintf(2, \"florence: helper   #%d %p  %s+%#lx  (%s)\\n\", n, reinterpret_cast<void*>(address), di.dli_sname ? di.dli_sname : \"?\", di.dli_saddr ? static_cast<unsigned long>(address - reinterpret_cast<uintptr_t>(di.dli_saddr)) : 0UL, di.dli_fname);\n        else\n            dprintf(2, \"florence: helper   #%d %p\\n\", n, reinterpret_cast<void*>(address));\n    };\n    dprintf(2, \"florence: helper process %d crashed: signal %d, fault address %p\\n\", getpid(), sig, info ? info->si_addr : nullptr);\n    describe(0, pc);\n    describe(1, lr);\n    for (int i = 2; i < 40 && fp && !(reinterpret_cast<uintptr_t>(fp) & 7) && reinterpret_cast<uintptr_t>(fp) > 0x10000; i++) {\n        describe(i, fp[1]);\n        if (fp[0] <= reinterpret_cast<uintptr_t>(fp))\n            break;\n        fp = reinterpret_cast<uintptr_t*>(fp[0]);\n    }\n    signal(sig, SIG_DFL);\n}\n#endif\n\nAuxiliaryProcessMainCommon::AuxiliaryProcessMainCommon()\n{\n#if ENABLE(BREAKPAD)\n    installBreakpadExceptionHandler();\n#endif\n#if OS(DARWIN)\n    for (int sig : { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP }) {\n        struct sigaction action { };\n        action.sa_sigaction = floHelperCrash;\n        action.sa_flags = SA_SIGINFO;\n        sigemptyset(&action.sa_mask);\n        sigaction(sig, &action, nullptr);\n    }\n#endif\n}",
     "Florence: crash report of a helper process")
edit("Source/WebKit/Shared/unix/AuxiliaryProcessMain.cpp",
     "#include <signal.h>\n#include <stdlib.h>\n#include <string.h>\n",
     "#include <signal.h>\n#include <stdlib.h>\n#include <string.h>\n#if OS(DARWIN)\n#include <dlfcn.h>\n#include <unistd.h>\n#endif\n",
     "#if OS(DARWIN)\n#include <dlfcn.h>")
# In the composited (GL) mode scrolling moved to the scrolling thread (setThreadedScrollingEnabled above), and the overlay scroll
# bars never appeared: they are driven from the main thread's scroll animator. WEBKIT_ASYNC_SCROLLING=0 keeps the GL compositor and
# scrolls on the main thread, as the CPU path does.
edit("Source/WebKit/UIProcess/wpe/WebPreferencesWPE.cpp",
     "    setThreadedScrollingEnabled(composited);\n",
     "    const char* asyncScrolling = getenv(\"WEBKIT_ASYNC_SCROLLING\");     // Florence\n    bool async = composited && !(asyncScrolling && !strcmp(asyncScrolling, \"0\"));\n    setThreadedScrollingEnabled(async);\n    setAsyncFrameScrollingEnabled(async);\n    setAsyncOverflowScrollingEnabled(async);\n",
     "Florence\n    bool async = composited")
PY
