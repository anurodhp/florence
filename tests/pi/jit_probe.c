/*
 * Florence: can this kernel run code written at run time (what a JIT does)? Step 1 of docs/jit-plan.md. One probe per process,
 * because the interesting outcomes are crashes:  jit_probe <probe>   (run each in turn; the shell reports the signal)
 *   ctr      read CTR_EL0 (the cache line sizes): needs SCTLR_EL1.UCT
 *   dc       dc cvau on a data address: needs SCTLR_EL1.UCI
 *   ic       ic ivau on a data address: needs SCTLR_EL1.UCI
 *   rwx      anonymous PROT_READ|WRITE|EXEC mapping, write code, run it
 *   jit      the same with MAP_JIT
 *   rw-rx    map RW, write code, mprotect to RX, run (WebKit's way without fast permissions)
 * The code is `mov w0, #42; ret` (arm64). The instruction cache is invalidated with the architected sequence at a fixed 16 byte stride
 * (valid for any line size), without reading CTR_EL0.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_JIT
#define MAP_JIT 0x0800
#endif

static const uint32_t code[] = { 0x52800540 /* mov w0, #42 */, 0xd65f03c0 /* ret */ };

static void sync_code(void *p, size_t len)
{
	uintptr_t a, end = (uintptr_t)p + len;
	for (a = (uintptr_t)p & ~(uintptr_t)15; a < end; a += 16)
		__asm__ volatile("dc cvau, %0" : : "r"(a) : "memory");
	__asm__ volatile("dsb ish" : : : "memory");
	for (a = (uintptr_t)p & ~(uintptr_t)15; a < end; a += 16)
		__asm__ volatile("ic ivau, %0" : : "r"(a) : "memory");
	__asm__ volatile("dsb ish\n\tisb" : : : "memory");
}

static void try(int prot, int flags, int then_protect)
{
	size_t len = 16384;
	void *p = mmap(NULL, len, prot, flags | MAP_PRIVATE | MAP_ANON, -1, 0);
	int rc;

	if (p == MAP_FAILED) { printf("mmap failed: %s\n", strerror(errno)); return; }
	printf("mapped at %p\n", p);
	memcpy(p, code, sizeof code);
	printf("code written\n");
	if (then_protect) {
		if (mprotect(p, len, PROT_READ | PROT_EXEC) != 0) { printf("mprotect RX failed: %s\n", strerror(errno)); return; }
		printf("mprotect to RX ok\n");
	}
	sync_code(p, sizeof code);
	printf("instruction cache synchronised\n");
	rc = ((int (*)(void))p)();
	printf("ran, returned %d %s\n", rc, rc == 42 ? "(OK)" : "(WRONG)");
}

int main(int argc, char **argv)
{
	static char data[256];
	const char *w = argc > 1 ? argv[1] : "";
	uint64_t ctr = 0;

	setvbuf(stdout, NULL, _IONBF, 0);
	printf("page size %ld, probe %s\n", (long)getpagesize(), w);
	if (!strcmp(w, "ctr")) { __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr)); printf("CTR_EL0 = %#llx (OK)\n", (unsigned long long)ctr); }
	else if (!strcmp(w, "dc")) { __asm__ volatile("dc cvau, %0" : : "r"(data) : "memory"); printf("dc cvau (OK)\n"); }
	else if (!strcmp(w, "ic")) { __asm__ volatile("ic ivau, %0" : : "r"(data) : "memory"); printf("ic ivau (OK)\n"); }
	else if (!strcmp(w, "rwx")) try(PROT_READ | PROT_WRITE | PROT_EXEC, 0, 0);
	else if (!strcmp(w, "jit")) try(PROT_READ | PROT_WRITE | PROT_EXEC, MAP_JIT, 0);
	else if (!strcmp(w, "rw-rx")) try(PROT_READ | PROT_WRITE, 0, 1);
	else { printf("usage: jit_probe ctr|dc|ic|rwx|jit|rw-rx\n"); return 2; }
	return 0;
}
