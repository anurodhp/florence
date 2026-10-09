/*
 * Florence: speed of the C library's memory functions on the Pi (MB/s), at the sizes programs use. Also checks results against a
 * reference loop, so a replacement is verified as well as timed.   mem_bench
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static double now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static volatile unsigned long sink;

#define BENCH(label, size, off, stmt) do { \
	size_t sz = (size); long it = (long)((8u << 20) / (sz < 64 ? 64 : sz)); long q; double t0, dt; \
	if (it < 4) it = 4; \
	t0 = now_ms(); for (q = 0; q < it; q++) { stmt; } dt = now_ms() - t0; \
	printf("%-30s %8zu B  %9.1f MB/s\n", label, sz, (sz * (double)it / 1048576.0) / (dt / 1e3)); (void)off; } while (0)

int main(void)
{
	static const size_t sizes[] = { 16, 100, 1000, 16384, 262144, 2u << 20 };
	size_t i, n = 4u << 20;
	unsigned char *a = malloc(n + 64), *b = malloc(n + 64);
	int bad = 0;

	memset(a, 0x5a, n + 64); memset(b, 0x5a, n + 64);
	for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
		size_t s = sizes[i];
		BENCH("memset", s, 0, memset(a, (int)(q & 255), s));
		BENCH("bzero", s, 0, bzero(a, s));
		BENCH("memcpy (aligned)", s, 0, memcpy(b, a, s));
		BENCH("memcpy (src+1, dst+3)", s, 0, memcpy(b + 3, a + 1, s - 8));
		BENCH("memmove (overlap, forward)", s, 0, memmove(a + 7, a, s - 8));
		BENCH("memmove (overlap, backward)", s, 0, memmove(a, a + 7, s - 8));
		BENCH("memcmp (equal)", s, 0, sink += (unsigned long)memcmp(a, b, s));
		BENCH("memchr (not found)", s, 0, sink += (unsigned long)(size_t)memchr(a, 0x11, s));
	}
	/* correctness: every size 0..300 x source/dest misalignment 0..15, memset/memcpy/memmove/memcmp/memchr against byte loops */
	{
		unsigned char *x = malloc(1024), *y = malloc(1024), *r = malloc(1024);
		size_t len, so, dofs;
		for (len = 0; len <= 300 && !bad; len++)
			for (so = 0; so < 16 && !bad; so++)
				for (dofs = 0; dofs < 16 && !bad; dofs++) {
					size_t k;
					for (k = 0; k < 1024; k++) { x[k] = (unsigned char)(k * 7 + 3); y[k] = 0xEE; r[k] = 0xEE; }
					memcpy(y + dofs, x + so, len);
					for (k = 0; k < len; k++) r[dofs + k] = x[so + k];
					if (memcmp(y, r, 1024) != 0) { printf("FAIL memcpy len %zu src+%zu dst+%zu\n", len, so, dofs); bad++; }
					memset(y + dofs, 0xAB, len);
					for (k = 0; k < 1024; k++) r[k] = 0xEE;
					for (k = 0; k < len; k++) r[dofs + k] = 0xAB;
					for (k = 0; k < 1024; k++) if (y[k] != (k >= dofs && k < dofs + len ? 0xAB : (k >= dofs + len || k < dofs ? ((k >= dofs + len) ? y[k] : y[k]) : 0))) break;
					for (k = 0; k < 1024; k++) { y[k] = (unsigned char)(k * 5 + 1); r[k] = y[k]; }
					memmove(y + dofs, y + so, len);       /* overlapping, either direction */
					{ unsigned char *tmp = malloc(len + 1); for (k = 0; k < len; k++) tmp[k] = r[so + k]; for (k = 0; k < len; k++) r[dofs + k] = tmp[k]; free(tmp); }
					if (memcmp(y, r, 1024) != 0) { printf("FAIL memmove len %zu src+%zu dst+%zu\n", len, so, dofs); bad++; }
					for (k = 0; k < 1024; k++) y[k] = 0x77;
					memset(y + dofs, 0, len);
					for (k = 0; k < 1024; k++) if (y[k] != ((k >= dofs && k < dofs + len) ? 0 : 0x77)) { printf("FAIL memset len %zu dst+%zu at %zu\n", len, dofs, k); bad++; break; }
					if (len > 0) {
						void *p;
						for (k = 0; k < 1024; k++) y[k] = 1;
						y[so + len - 1] = 9;
						p = memchr(y + so, 9, len);
						if (p != y + so + len - 1) { printf("FAIL memchr len %zu +%zu\n", len, so); bad++; }
						if (memchr(y + so, 2, len) != NULL) { printf("FAIL memchr (absent) len %zu +%zu\n", len, so); bad++; }
						x[so + len - 1] ^= 1; memcpy(y + dofs, x + so, len); x[so + len - 1] ^= 1;
						if (memcmp(x + so, y + dofs, len) == 0) { printf("FAIL memcmp (differs at end) len %zu\n", len); bad++; }
						if (memcmp(x + so, x + so, len) != 0) { printf("FAIL memcmp (same) len %zu\n", len); bad++; }
					}
				}
		printf("correctness (sizes 0-300, 16x16 alignments, memcpy/memmove/memset/memchr/memcmp): %s\n", bad ? "FAILED" : "all ok");
	}
	/* the string functions, which come from the same place: correctness at every length 0..300 and alignment, then speed */
	{
		char *x = malloc(1024), *y = malloc(1024);
		size_t len, so, k;
		int sbad = 0;
		for (len = 0; len <= 300 && !sbad; len++)
			for (so = 0; so < 16 && !sbad; so++) {
				for (k = 0; k < 1024; k++) { x[k] = (char)('a' + (k % 23)); y[k] = x[k]; }
				x[so + len] = 0; y[(so + 3) % 16 + len] = 0;                       /* both strings are len long, different alignment */
				{
					char *a = x + so, *b = y + (so + 3) % 16;
					size_t j;
					for (j = 0; j < len; j++) b[j] = a[j];
					if (strlen(a) != len) { printf("FAIL strlen len %zu +%zu\n", len, so); sbad++; }
					if (strnlen(a, 1000) != len || strnlen(a, len / 2) != len / 2) { printf("FAIL strnlen len %zu +%zu\n", len, so); sbad++; }
					if (strcmp(a, b) != 0 || strncmp(a, b, 1000) != 0) { printf("FAIL strcmp (equal) len %zu +%zu\n", len, so); sbad++; }
					if (len > 0) {
						b[len - 1] ^= 1;
						if (strcmp(a, b) == 0 || (strcmp(a, b) < 0) != ((unsigned char)a[len - 1] < (unsigned char)b[len - 1])) { printf("FAIL strcmp (differs) len %zu +%zu\n", len, so); sbad++; }
						if (strncmp(a, b, len - 1) != 0 || strncmp(a, b, len) == 0) { printf("FAIL strncmp len %zu +%zu\n", len, so); sbad++; }
						b[len - 1] ^= 1;
						if (strchr(a, a[len - 1]) == NULL || strchr(a, a[len - 1]) > a + len - 1) { printf("FAIL strchr (found) len %zu +%zu\n", len, so); sbad++; }
						if (strchr(a, '~') != NULL) { printf("FAIL strchr (absent) len %zu +%zu\n", len, so); sbad++; }
					}
					if (strchr(a, 0) != a + len) { printf("FAIL strchr(NUL) len %zu +%zu\n", len, so); sbad++; }
					{
						char dst[400];
						memset(dst, '#', sizeof dst);
						if (strcpy(dst + 5, a) != dst + 5 || memcmp(dst + 5, a, len + 1) != 0 || dst[4] != '#' || dst[5 + len + 1] != '#') { printf("FAIL strcpy len %zu +%zu\n", len, so); sbad++; }
					}
				}
			}
		printf("correctness (strlen/strnlen/strcmp/strncmp/strchr/strcpy, lengths 0-300, 16 alignments): %s\n", sbad ? "FAILED" : "all ok");
		bad += sbad;
		{
			char *big = malloc(1u << 20), *big2 = malloc(1u << 20);
			memset(big, 'x', (1u << 20) - 1); big[(1u << 20) - 1] = 0; memcpy(big2, big, 1u << 20);
			BENCH("strlen", 1000, 0, (big[999] = 0, sink += strlen(big), big[999] = 'x'));
			BENCH("strlen", 1u << 20, 0, sink += strlen(big));
			BENCH("strcmp (equal)", 1u << 20, 0, sink += (unsigned long)strcmp(big, big2));
			BENCH("strchr (not found)", 1u << 20, 0, sink += (unsigned long)(size_t)strchr(big, 'y'));
		}
	}
	return bad != 0;
}
