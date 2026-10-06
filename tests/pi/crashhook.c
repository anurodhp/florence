/* SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel) */
/* DYLD_INSERT_LIBRARIES=/usr/local/lib/libcrashhook.1.dylib: every process (WebKit's helpers included) prints a crash report
 * (flo_diag.c) on a fatal signal. For finding why a helper dies silently. */
void flo_install_crash_report(void);
__attribute__((constructor)) static void hook(void) { flo_install_crash_report(); }
