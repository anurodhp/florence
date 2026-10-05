/* SPDX-License-Identifier: APSL-2.0 (interface declarations modelled on Apple's XNU / macOS headers, which are APSL-2.0; see LICENSE) */
/* What GLib 2.78 gspawn.c takes from <sys/proc_info.h>; the Xcode 12 iPhoneOS SDK has neither this nor libproc.h.
 * Copied from the port's kernel: third_party/xnu-7195/bsd/sys/proc_info.h:725-728 (struct proc_fdinfo), :737 (PROC_PIDLISTFDS). */
#ifndef FLO_SYS_PROC_INFO_H
#define FLO_SYS_PROC_INFO_H
#include <stdint.h>
struct proc_fdinfo { int32_t proc_fd; uint32_t proc_fdtype; };
#define PROC_PIDLISTFDS 1
#define PROC_PIDLISTFD_SIZE (sizeof(struct proc_fdinfo))   /* :738 */
#endif
