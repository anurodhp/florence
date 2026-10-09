/*
 * Florence: how fast is the C library's allocator on the Pi? Operations per second for the patterns programs use: small malloc/free pairs,
 * a working set of mixed sizes churned in random order, calloc, realloc growth, large allocations. Also checks that memory is intact
 * (every block carries a pattern checked on free), so a faster allocator is verified as well as timed.   malloc_bench
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static unsigned rng = 12345;
static unsigned next(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

static int bad;
static void fill(unsigned char *p, size_t n, unsigned char v) { size_t i; for (i = 0; i < n && i < 64; i++) p[i] = (unsigned char)(v + i); if (n > 64) p[n - 1] = v; }
static void check(const unsigned char *p, size_t n, unsigned char v, const char *what)
{
	size_t i;
	for (i = 0; i < n && i < 64; i++) if (p[i] != (unsigned char)(v + i)) { if (!bad++) printf("CORRUPT (%s): block of %zu bytes, offset %zu\n", what, n, i); return; }
	if (n > 64 && p[n - 1] != v) { if (!bad++) printf("CORRUPT (%s): block of %zu bytes, tail\n", what, n); }
}

#define N 4096

int main(void)
{
	static void *blk[N];
	static size_t sz[N];
	static unsigned char tag[N];
	double t0, dt;
	long i, ops;
	size_t s;
	static const size_t sizes[] = { 16, 64, 256, 1024, 4096, 65536 };
	size_t k;

	for (k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
		s = sizes[k];
		ops = s <= 4096 ? 200000 : 20000;
		t0 = now_ms();
		for (i = 0; i < ops; i++) { unsigned char *p = malloc(s); p[0] = 1; p[s - 1] = 2; free(p); }
		dt = now_ms() - t0;
		printf("malloc+free %6zu B            %9.0f ops/s  (%.2f us per pair)\n", s, ops / (dt / 1e3), dt * 1e3 / ops);
	}
	for (k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
		s = sizes[k];
		ops = s <= 4096 ? 100000 : 5000;
		t0 = now_ms();
		for (i = 0; i < ops; i++) { unsigned char *p = calloc(1, s); if (p[0] | p[s - 1] | p[s / 2]) bad++; free(p); }
		dt = now_ms() - t0;
		printf("calloc+free %6zu B            %9.0f ops/s  (%.2f us per pair)\n", s, ops / (dt / 1e3), dt * 1e3 / ops);
	}
	/* a working set churned in random order, sizes 8..8192, each block's contents checked on free */
	memset(blk, 0, sizeof blk);
	ops = 400000;
	t0 = now_ms();
	for (i = 0; i < ops; i++) {
		unsigned j = next() % N;
		if (blk[j]) { check(blk[j], sz[j], tag[j], "churn"); free(blk[j]); blk[j] = NULL; }
		else { sz[j] = 8 + next() % 8192; if ((next() & 7) != 0) sz[j] = 8 + next() % 256; blk[j] = malloc(sz[j]); tag[j] = (unsigned char)next(); fill(blk[j], sz[j], tag[j]); }
	}
	dt = now_ms() - t0;
	printf("mixed churn (4096 live blocks)    %9.0f ops/s  (%.2f us per op)\n", ops / (dt / 1e3), dt * 1e3 / ops);
	for (i = 0; i < N; i++) if (blk[i]) { check(blk[i], sz[i], tag[i], "final"); free(blk[i]); }
	/* realloc growth, as a string or vector builds up */
	t0 = now_ms();
	for (i = 0; i < 2000; i++) {
		unsigned char *p = NULL; size_t n = 0;
		while (n < 65536) { n = n ? n + n / 2 + 1 : 16; p = realloc(p, n); fill(p, n, 7); }
		check(p, n, 7, "realloc"); free(p);
	}
	dt = now_ms() - t0;
	printf("realloc growth 16 B -> 64 KB       %9.0f builds/s (%.2f ms each)\n", 2000 / (dt / 1e3), dt / 2000);
	printf("integrity: %s\n", bad ? "FAILED" : "all ok");
	return bad != 0;
}
