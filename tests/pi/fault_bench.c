/*
 * Florence: what does a page fault cost on this kernel? (A fresh 2.3 MB buffer cost ~100 ms on the Pi, docs/webkit-patches.md.)
 *   fault_bench [MB]     anonymous memory: first touch (zero-fill fault) vs second touch vs memset of faulted memory,
 *                        malloc+memset+free cycles of a frame-sized buffer, and the fault counts the kernel reports.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

static double now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static long minflt(void)
{
	struct rusage r;
	getrusage(RUSAGE_SELF, &r);
	return r.ru_minflt;
}

static double cpu_ms(int sys)
{
	struct rusage r;
	getrusage(RUSAGE_SELF, &r);
	struct timeval t = sys ? r.ru_stime : r.ru_utime;
	return t.tv_sec * 1e3 + t.tv_usec / 1e3;
}

int main(int argc, char **argv)
{
	size_t mb = argc > 1 ? (size_t)atoi(argv[1]) : 32, n = mb << 20, page = (size_t)getpagesize(), i;
	volatile unsigned char *p;
	double t, s0;
	long f0;
	int k;

	printf("page size %zu, %zu MB = %zu pages\n", page, mb, n / page);
	p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
	if (p == MAP_FAILED) { perror("mmap"); return 1; }

	f0 = minflt(); s0 = cpu_ms(1); t = now_ms();
	for (i = 0; i < n; i += page) p[i] = 1;
	t = now_ms() - t;
	printf("first touch (zero-fill faults): %.1f ms = %.1f us/page, %ld faults counted, %.1f ms of it in the kernel\n",
	       t, t * 1e3 / (n / page), minflt() - f0, cpu_ms(1) - s0);

	t = now_ms();
	for (i = 0; i < n; i += page) p[i] = 2;
	t = now_ms() - t;
	printf("second touch (no fault):        %.2f ms = %.2f us/page\n", t, t * 1e3 / (n / page));

	t = now_ms();
	memset((void *)p, 3, n);
	t = now_ms() - t;
	printf("memset of faulted memory:       %.1f ms = %.0f MB/s\n", t, mb / (t / 1e3));

	t = now_ms();
	memset((void *)p, 0, n);
	t = now_ms() - t;
	printf("memset 0 of faulted memory:     %.1f ms = %.0f MB/s\n", t, mb / (t / 1e3));
	munmap((void *)p, n);

	/* is it memset, or the memory? plain loops, and the library's memcpy */
	{
		size_t m = 4u << 20, w = m / 8;
		volatile unsigned long *a = malloc(m), *b = malloc(m);
		unsigned long sum = 0;
		double tt;
		for (i = 0; i < w; i++) { a[i] = i; b[i] = i; }
		tt = now_ms(); for (k = 0; k < 4; k++) for (i = 0; i < w; i++) a[i] = k; tt = now_ms() - tt;
		printf("store64 loop, 4 MB x4:   %.0f MB/s\n", 16.0 / (tt / 1e3));
		tt = now_ms(); for (k = 0; k < 4; k++) for (i = 0; i < w; i++) sum += a[i]; tt = now_ms() - tt;
		printf("load64 loop, 4 MB x4:    %.0f MB/s (sum %lu)\n", 16.0 / (tt / 1e3), sum & 1);
		tt = now_ms(); for (k = 0; k < 4; k++) memcpy((void *)b, (void *)a, m); tt = now_ms() - tt;
		printf("memcpy 4 MB x4:          %.0f MB/s\n", 16.0 / (tt / 1e3));
		tt = now_ms(); for (k = 0; k < 4; k++) memset((void *)a, 0, m); tt = now_ms() - tt;
		printf("memset 4 MB x4:          %.0f MB/s\n", 16.0 / (tt / 1e3));
		{
			/* small: fits in L1/L2 if the memory is cacheable */
			size_t sm = 16u << 10, sw = sm / 8;
			tt = now_ms(); for (k = 0; k < 2000; k++) for (i = 0; i < sw; i++) a[i] = k; tt = now_ms() - tt;
			printf("store64 loop, 16 KB x2000: %.0f MB/s\n", (sm * 2000.0 / 1048576.0) / (tt / 1e3));
			tt = now_ms(); for (k = 0; k < 2000; k++) memset((void *)a, 0, sm); tt = now_ms() - tt;
			printf("memset 16 KB x2000:        %.0f MB/s\n", (sm * 2000.0 / 1048576.0) / (tt / 1e3));
		}
	}

	/* the readback's pattern: a frame-sized buffer allocated, zeroed and freed every frame */
	{
		size_t fb = 960 * 602 * 4;
		double ta = 0;
		long f1 = minflt();
		for (k = 0; k < 20; k++) {
			double t1 = now_ms();
			unsigned char *b = malloc(fb);
			memset(b, 0, fb);
			free(b);
			ta += now_ms() - t1;
		}
		printf("malloc+memset+free of %zu KB: %.1f ms per cycle, %ld faults per cycle\n", fb >> 10, ta / 20, (minflt() - f1) / 20);
	}
	/* the same with the buffer kept (what a fixed reader does) */
	{
		size_t fb = 960 * 602 * 4;
		unsigned char *b = malloc(fb);
		double ta = 0;
		memset(b, 0, fb);
		for (k = 0; k < 20; k++) {
			double t1 = now_ms();
			memset(b, 0, fb);
			ta += now_ms() - t1;
		}
		printf("memset of a reused %zu KB buffer: %.1f ms per cycle\n", fb >> 10, ta / 20);
		free(b);
	}
	return 0;
}
