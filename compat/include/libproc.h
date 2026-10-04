/* SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel) */
/* proc_pidinfo, exported by the port's libsystem_kernel.dylib, as GLib 2.78 gspawn.c uses it to close inherited fds. */
#ifndef FLO_LIBPROC_H
#define FLO_LIBPROC_H
#include <stdint.h>
#include <sys/proc_info.h>
int proc_pidinfo(int pid, int flavor, uint64_t arg, void *buffer, int buffersize);
#endif
