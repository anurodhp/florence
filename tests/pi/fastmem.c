/* SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel) */
/* DYLD_INSERT_LIBRARIES shim: a fast memset/bzero. The image's (libplatform's generic _platform_memset, built -O0) goes through
 * _platform_memset_pattern4, which calls _platform_memmove once per 4 bytes: ~24 MB/s (tests/pi/fault_bench.c), so every zero-fill of a
 * frame-sized buffer costs ~95 ms. This stores 64 bytes per iteration. A test of what a fixed libsystem_platform would give; the fix itself
 * belongs in the iokit port. Interposes memset, bzero and the _platform_ names libsystem_malloc and friends call. */
#include <stddef.h>
#include <stdint.h>

static void *fast_memset(void *b, int c, size_t len)
{
	unsigned char *p = b;
	uint64_t v = (unsigned char)c * 0x0101010101010101ULL;

	while (len != 0 && ((uintptr_t)p & 15) != 0) {
		*(volatile unsigned char *)p++ = (unsigned char)c;
		len--;
	}
	while (len >= 64) {
		__asm__ volatile("stp %1, %1, [%0]\n\tstp %1, %1, [%0, #16]\n\tstp %1, %1, [%0, #32]\n\tstp %1, %1, [%0, #48]"
				 : : "r"(p), "r"(v) : "memory");
		p += 64; len -= 64;
	}
	while (len >= 8) {
		*(volatile uint64_t *)p = v;
		p += 8; len -= 8;
	}
	while (len != 0) {
		*(volatile unsigned char *)p++ = (unsigned char)c;
		len--;
	}
	return b;
}

/* FASTMEM_STATS=1: count the bytes that go through memset/bzero and report every 1 MB ("fastmem: pid N: X MB in Y calls, Z MB in calls >= 64 KB"),
 * to see what the slow library memset costs a real program (bytes / 24 MB/s). Counted without locks: approximate under threads. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
static unsigned long st_bytes, st_calls, st_big, st_next = 1ul << 20;
static int st_on;   /* off until the constructor below: memset is called by dyld before libSystem is initialised */
__attribute__((constructor)) static void st_init(void) { st_on = getenv("FASTMEM_STATS") != NULL; }
static int st_busy;   /* snprintf below calls memset */
static void count(size_t n)
{
	if (!st_on || st_busy)
		return;
	st_busy = 1;
	st_bytes += n; st_calls++;
	if (n >= 65536)
		st_big += n;
	if (st_bytes >= st_next) {
		char b[160];
		int k = snprintf(b, sizeof b, "fastmem: pid %d: %lu MB in %lu calls, %lu MB in calls >= 64 KB\n", getpid(), st_bytes >> 20, st_calls, st_big >> 20);
		write(2, b, k);
		st_next = st_bytes + (1ul << 20);
	}
	st_busy = 0;
}

static void *my_memset(void *b, int c, size_t len) { count(len); return fast_memset(b, c, len); }
static void my_bzero(void *s, size_t n) { count(n); fast_memset(s, 0, n); }

extern void *memset(void *, int, size_t);
extern void bzero(void *, size_t);
extern void *_platform_memset(void *, int, size_t);
extern void _platform_bzero(void *, size_t);

struct interpose { const void *replacement, *replacee; };
__attribute__((used)) static const struct interpose tab[] __attribute__((section("__DATA,__interpose"))) = {
	{ (const void *)my_memset, (const void *)memset },
	{ (const void *)my_bzero, (const void *)bzero },
	{ (const void *)my_memset, (const void *)_platform_memset },
	{ (const void *)my_bzero, (const void *)_platform_bzero },
};
