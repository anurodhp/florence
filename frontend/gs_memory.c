/*
 * Florence: low-memory handling. The iokit port has no memory-pressure notification path (no
 * memorystatus delivery, so a libdispatch DISPATCH_SOURCE_TYPE_MEMORYPRESSURE source would never fire),
 * and there is no swap, so running out is fatal. Instead the core asks the kernel how much is left at
 * moments when memory has just changed hands (a page finished loading, a tab closed) and, when little
 * is, has NetSurf drop every cached object nobody is using. No timer polls: see flo_memory_note().
 *
 * The kernel is asked through host_statistics64() found with dlsym(), so a system without it (or a build
 * where it is not exported) simply never purges, instead of failing to link or start.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only
 */
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include "content/llcache.h"
#include "gnustep/gs.h"

/* the start of struct vm_statistics64 (mach/vm_statistics.h, #pragma pack(4)): only these four 32-bit
 * page counts are read, which are the first fields, so nothing here depends on the rest of the layout */
struct vmstat_head { uint32_t free_count, active_count, inactive_count, wire_count; };
#define HOST_VM_INFO64 4
#define VMSTAT_BUF_INTS 40                      /* HOST_VM_INFO64_COUNT is 38; room to spare */

typedef unsigned int (*mach_host_self_fn)(void);
typedef int (*host_statistics64_fn)(unsigned int host, int flavor, int *info, unsigned int *count);

/* Thresholds as a fraction of RAM, applied to pages the kernel could give out without evicting
 * anything we hold (free + inactive). Pure so a test can drive it. */
enum { MEM_OK, MEM_LOW };

int gs_memory_level_for(uint64_t avail_bytes, uint64_t total_bytes)
{
	if (total_bytes == 0)
		return MEM_OK;
	return avail_bytes * 100 < total_bytes * 15 ? MEM_LOW : MEM_OK;     /* under 15 % of RAM */
}

/* MEM_OK when it cannot be measured */
int gs_memory_level(void)
{
	static bool probed, usable;
	static mach_host_self_fn host_self;
	static host_statistics64_fn host_stats;
	static uint64_t total;
	int buf[VMSTAT_BUF_INTS];
	unsigned int count = VMSTAT_BUF_INTS;
	struct vmstat_head v;
	uint64_t avail;
	long page;

	if (!probed) {
		size_t len = sizeof(total);

		probed = true;
		host_self = (mach_host_self_fn)dlsym(RTLD_DEFAULT, "mach_host_self");
		host_stats = (host_statistics64_fn)dlsym(RTLD_DEFAULT, "host_statistics64");
		if (sysctlbyname("hw.memsize", &total, &len, NULL, 0) != 0)
			total = 0;
		usable = host_self != NULL && host_stats != NULL && total != 0;
		flo_trace(usable ? "memory: low-memory checks on" : "memory: cannot measure free memory, checks off");
	}
	if (!usable || host_stats(host_self(), HOST_VM_INFO64, buf, &count) != 0 || count < 4)
		return MEM_OK;
	memcpy(&v, buf, sizeof(v));
	page = sysconf(_SC_PAGESIZE);
	avail = ((uint64_t)v.free_count + v.inactive_count) * (uint64_t)(page > 0 ? page : 4096);
	return gs_memory_level_for(avail, total);
}

/* every cached object nothing is using (the back/forward and recently-visited cache) */
void gs_memory_purge(void)
{
	llcache_clean(true);
}
