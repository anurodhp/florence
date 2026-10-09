/*
 * Florence: correctness and speed of the C library's string functions on the Pi, checked against naive reference implementations written
 * here, over every length 0..N and every source/destination misalignment 0..15:
 *   strlen strnlen strcmp strncmp strchr strrchr strcpy stpcpy strlcpy strlcat strcat strncat strspn strcspn strpbrk strsep memccpy
 *   timingsafe_bcmp timingsafe_memcmp
 * And a guard-page test: each function reads a string that ends on the last byte of a mapped page followed by an unmapped one, which the
 * 16-byte-chunk SIMD routines must not read past. (memccpy and timingsafe_memcmp are looked up at run time: older images lack them.)
 *   str_test
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#include <dlfcn.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static unsigned rng = 20261009;
static unsigned rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

static int fails;
#define FAIL(...) do { if (fails++ < 20) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef void *(*memccpy_fn)(void *, const void *, int, size_t);
typedef int (*tsmemcmp_fn)(const void *, const void *, size_t);
static memccpy_fn p_memccpy;
static tsmemcmp_fn p_tsmemcmp;

/* ---- naive references ---- */
static size_t r_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static size_t r_strspn(const char *s, const char *set) { size_t n = 0; for (; s[n]; n++) if (!strchr(set, s[n]) || !set[0]) break; return n; }
static size_t r_strcspn(const char *s, const char *set) { size_t n = 0; for (; s[n]; n++) { const char *p; int hit = 0; for (p = set; *p; p++) if (*p == s[n]) hit = 1; if (hit) break; } return n; }
static int sgn(int x) { return (x > 0) - (x < 0); }

/* a random string of length len from a small alphabet (so equal prefixes and set hits are common), bytes >= 0x80 included */
static void mkstr(char *p, size_t len)
{
	static const unsigned char alpha[] = { 'a', 'b', 'c', 'd', 'e', ' ', ',', 0x80, 0xC3, 0xFF, 'z' };
	size_t i;
	for (i = 0; i < len; i++) p[i] = (char)alpha[rnd() % sizeof alpha];
	p[len] = 0;
}

static char A[2048], B[2048], C[2048], R[2048];

static void test_basic(void)
{
	size_t len, so, dofs, k;
	for (len = 0; len <= 140; len++)
		for (so = 0; so < 16; so++)
			for (dofs = 0; dofs < 16; dofs += 3) {
				char *s = A + so, *t = B + dofs, *d = C + dofs;
				size_t tl;
				mkstr(s, len);
				/* t: a string of the same or different length, sharing a prefix with s half the time */
				if (rnd() & 1) { memcpy(t, s, len + 1); if (len && (rnd() & 1)) t[rnd() % len] ^= (char)(1 + rnd() % 3); if (rnd() % 5 == 0) t[len / 2] = 0; }
				else mkstr(t, rnd() % 150);
				tl = r_strlen(t);

				if (strlen(s) != len) FAIL("strlen len %zu +%zu", len, so);
				for (k = 0; k < 4; k++) { size_t n = k == 0 ? 0 : (rnd() % (len + 20)); size_t want = len < n ? len : n; if (strnlen(s, n) != want) FAIL("strnlen len %zu n %zu +%zu", len, n, so); }
				{ int want = 0; size_t i; for (i = 0; ; i++) { unsigned char x = (unsigned char)s[i], y = (unsigned char)t[i]; if (x != y) { want = x < y ? -1 : 1; break; } if (!x) break; }
				  if (sgn(strcmp(s, t)) != want) FAIL("strcmp len %zu/%zu +%zu/%zu", len, tl, so, dofs); }
				for (k = 0; k < 4; k++) { size_t n = rnd() % (len + 20); int want = 0; size_t i; for (i = 0; i < n; i++) { unsigned char x = (unsigned char)s[i], y = (unsigned char)t[i]; if (x != y) { want = x < y ? -1 : 1; break; } if (!x) break; }
				  if (sgn(strncmp(s, t, n)) != want) FAIL("strncmp len %zu/%zu n %zu", len, tl, n); }
				{ int c = "abcz\x80\xFFq"[rnd() % 7]; const char *w = NULL, *wl = NULL; size_t i; for (i = 0; ; i++) { if (s[i] == (char)c) { if (!w) w = s + i; wl = s + i; } if (!s[i]) break; }
				  if (strchr(s, c) != w) FAIL("strchr len %zu c %d +%zu", len, c, so);
				  if (strrchr(s, c) != wl) FAIL("strrchr len %zu c %d +%zu", len, c, so);
				  if (strchr(s, 0) != s + len || strrchr(s, 0) != s + len) FAIL("strchr/strrchr(NUL) len %zu +%zu", len, so); }

				/* copies */
				memset(d, '#', 400); strcpy(d, s);
				if (memcmp(d, s, len + 1) != 0 || d[len + 1] != '#') FAIL("strcpy len %zu +%zu/%zu", len, so, dofs);
				memset(d, '#', 400);
				if (stpcpy(d, s) != d + len || memcmp(d, s, len + 1) != 0 || d[len + 1] != '#') FAIL("stpcpy len %zu", len);
				for (k = 0; k < 5; k++) {
					size_t n = k == 0 ? 0 : rnd() % (len + 12);
					size_t want = len;
					memset(d, '#', 400); memset(R, '#', 400);
					if (n) { size_t m = len < n - 1 ? len : n - 1; memcpy(R, s, m); R[m] = 0; }
					if (strlcpy(d, s, n) != want || memcmp(d, R, 400) != 0) FAIL("strlcpy len %zu n %zu +%zu/%zu", len, n, so, dofs);
				}
				/* strlcat: dst holds a string of random length */
				for (k = 0; k < 5; k++) {
					size_t dl = rnd() % 30, n = rnd() % (dl + len + 12), want, m;
					memset(d, '#', 400); mkstr(d, dl); memcpy(R, d, 400);
					if (n <= dl) want = n + len;   /* dst not terminated within n (or n == dl): returns n + strlen(src) */
					else want = dl + len;
					if (n > dl) { m = len < n - dl - 1 ? len : n - dl - 1; memcpy(R + dl, s, m); R[dl + m] = 0; }
					/* when n <= dl strlcat leaves dst alone only if no NUL in the first n bytes: here dl < n means NUL inside; n <= dl means none */
					if (strlcat(d, s, n) != want || memcmp(d, R, 400) != 0) FAIL("strlcat dl %zu len %zu n %zu +%zu", dl, len, n, so);
				}
				for (k = 0; k < 3; k++) {
					size_t dl = rnd() % 30;
					memset(d, '#', 400); mkstr(d, dl); memcpy(R, d, 400); memcpy(R + dl, s, len + 1);
					if (strcat(d, s) != d || memcmp(d, R, 400) != 0) FAIL("strcat dl %zu len %zu", dl, len);
					{ size_t n = rnd() % (len + 10), m = len < n ? len : n;
					  memset(d, '#', 400); mkstr(d, dl); memcpy(R, d, 400); memcpy(R + dl, s, m); R[dl + m] = 0;
					  if (strncat(d, s, n) != d || memcmp(d, R, 400) != 0) FAIL("strncat dl %zu len %zu n %zu", dl, len, n); }
				}
				/* sets */
				for (k = 0; k < 4; k++) {
					char set[12]; size_t sl = rnd() % 6, i;
					for (i = 0; i < sl; i++) set[i] = "abcdz\x80\xC3\xFF, "[rnd() % 11];
					set[sl] = 0;
					{ size_t w = r_strspn(s, set); if (!sl) w = 0; if (strspn(s, set) != w) FAIL("strspn len %zu set '%s' +%zu: %zu vs %zu", len, set, so, strspn(s, set), w); }
					if (strcspn(s, set) != r_strcspn(s, set)) FAIL("strcspn len %zu set %zu +%zu", len, sl, so);
					{ size_t w = r_strcspn(s, set); const char *e = s[w] ? s + w : NULL; if (strpbrk(s, set) != e) FAIL("strpbrk len %zu set %zu", len, sl); }
					{ memcpy(d, s, len + 1); char *sp = d, *tok; size_t guard = 0, pos = 0, w;
					  while ((tok = strsep(&sp, set)) != NULL && guard++ < 400) {
						  w = r_strcspn(s + pos, set);
						  if (tok != d + pos || r_strlen(tok) != w) { FAIL("strsep len %zu set %zu token at %zu", len, sl, pos); break; }
						  pos += w + 1;
						  if (pos > len + 1) break;
					  } }
				}
				/* memccpy */
				if (p_memccpy) for (k = 0; k < 4; k++) {
					size_t n = rnd() % (len + 10); int c = "abz\x80\xFF"[rnd() % 5]; size_t i, stop = n; int found = 0;
					for (i = 0; i < n; i++) if (s[i] == (char)c) { stop = i + 1; found = 1; break; }
					memset(d, '#', 400); memset(R, '#', 400); memcpy(R, s, stop);
					{ void *rv = p_memccpy(d, s, c, n); void *want = found ? d + stop : NULL;
					  if (rv != want || memcmp(d, R, 400) != 0) FAIL("memccpy len %zu n %zu c %d", len, n, c); }
				}
			}
}

static void test_timingsafe(void)
{
	extern int timingsafe_bcmp(const void *, const void *, size_t);
	size_t n, so, dofs, k;
	for (n = 0; n <= 200; n++)
		for (so = 0; so < 16; so += 3)
			for (dofs = 0; dofs < 16; dofs += 5) {
				unsigned char *x = (unsigned char *)A + so, *y = (unsigned char *)B + dofs;
				for (k = 0; k < n; k++) x[k] = (unsigned char)rnd(), y[k] = x[k];
				if (timingsafe_bcmp(x, y, n) != 0) FAIL("timingsafe_bcmp equal n %zu", n);
				if (p_tsmemcmp && p_tsmemcmp(x, y, n) != 0) FAIL("timingsafe_memcmp equal n %zu", n);
				if (n) {
					size_t at = rnd() % n;
					y[at] = (unsigned char)(x[at] + 1 + rnd() % 200);
					if (timingsafe_bcmp(x, y, n) == 0) FAIL("timingsafe_bcmp differs at %zu of %zu", at, n);
					if (p_tsmemcmp && sgn(p_tsmemcmp(x, y, n)) != sgn(memcmp(x, y, n))) FAIL("timingsafe_memcmp sign n %zu at %zu", n, at);
				}
			}
}

/* ---- guard pages ---- */
static const char *cur = "(none)";
static void on_segv(int sig) { char b[160]; int n = snprintf(b, sizeof b, "GUARD FAIL: %s read past the end of its string (signal %d)\n", cur, sig); write(1, b, n); _exit(2); }

static void test_guard(void)
{
	long pg = sysconf(_SC_PAGESIZE);
	char *m = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
	char *end = m + pg;               /* first byte of the unmapped page */
	char *g2 = m;                     /* second string, for the two-string functions */
	size_t len;
	int c;
	char dst[300], set[] = "bd z";
	struct sigaction sa;

	memset(&sa, 0, sizeof sa); sa.sa_handler = on_segv; sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
	mprotect(m + pg, pg, PROT_NONE);
	(void)g2;
	for (len = 0; len <= 150; len++) {
		char *s = end - (len + 1);                /* the NUL is the last mapped byte */
		char *t = end - (len + 1);
		size_t i;
		for (i = 0; i < len; i++) s[i] = "abcdef"[i % 6];
		s[len] = 0;
		cur = "strlen"; if (strlen(s) != len) FAIL("guard strlen %zu", len);
		cur = "strnlen"; if (strnlen(s, 1000) != len) FAIL("guard strnlen %zu", len);
		cur = "strchr"; (void)strchr(s, 'q'); (void)strchr(s, 0);
		cur = "strrchr"; (void)strrchr(s, 'c');
		cur = "strcmp"; if (strcmp(s, t) != 0) FAIL("guard strcmp %zu", len);
		cur = "strncmp"; if (strncmp(s, t, 1000) != 0) FAIL("guard strncmp %zu", len);
		cur = "strcpy"; strcpy(dst, s);
		cur = "stpcpy"; (void)stpcpy(dst, s);
		cur = "strlcpy"; if (strlcpy(dst, s, sizeof dst) != len) FAIL("guard strlcpy %zu", len);
		cur = "strspn"; (void)strspn(s, "abc");
		cur = "strcspn"; (void)strcspn(s, set);
		cur = "strpbrk"; (void)strpbrk(s, set);
		cur = "strcat"; dst[0] = 0; strcat(dst, s);
		cur = "strncat"; dst[0] = 0; strncat(dst, s, 1000);
		cur = "strlcat"; dst[0] = 0; (void)strlcat(dst, s, sizeof dst);
		if (p_memccpy) { cur = "memccpy"; (void)p_memccpy(dst, s, 'q', len + 1); }
		/* the memory functions get exactly len bytes, so they must not read beyond the end either */
		cur = "memchr"; (void)memchr(s, 'q', len + 1);
		cur = "memcmp"; (void)memcmp(s, t, len + 1);
		cur = "memcpy"; memcpy(dst, s, len + 1);
		cur = "memmove"; memmove(dst, s, len + 1);
		cur = "timingsafe_bcmp"; { extern int timingsafe_bcmp(const void *, const void *, size_t); (void)timingsafe_bcmp(s, t, len + 1); }
		cur = "strsep"; { char *sp = dst; strcpy(dst, s); (void)strsep(&sp, "q"); }
		/* and writes: the destination ends on the last mapped byte */
		{ char *d = end - (len + 1);
		  cur = "strcpy (dst at page end)"; strcpy(d, s);
		  cur = "memset (dst at page end)"; memset(d, 'x', len + 1);
		  cur = "memcpy (dst at page end)"; memcpy(d, s, len + 1);
		  cur = "strlcpy (dst at page end)"; (void)strlcpy(d, "0123456789", len + 1 > 11 ? 11 : len + 1);
		  cur = "bzero (dst at page end)"; bzero(d, len + 1); }
	}
	(void)c;
	cur = "(none)";
	munmap(m, 2 * pg);
}

static volatile size_t sinkv;
#define BENCH(label, bytes, stmt) do { long it = 20000, q; double t0 = now_ms(), dt; for (q = 0; q < it; q++) { stmt; } dt = now_ms() - t0; \
	printf("%-28s %9.1f MB/s\n", label, ((double)(bytes) * it / 1048576.0) / (dt / 1e3)); } while (0)

int main(void)
{
	p_memccpy = (memccpy_fn)dlsym(RTLD_DEFAULT, "memccpy");
	p_tsmemcmp = (tsmemcmp_fn)dlsym(RTLD_DEFAULT, "timingsafe_memcmp");
	printf("memccpy %s, timingsafe_memcmp %s\n", p_memccpy ? "present" : "absent (skipped)", p_tsmemcmp ? "present" : "absent (skipped)");
	test_basic();
	test_timingsafe();
	printf("correctness (lengths 0-140 x alignments): %s\n", fails ? "FAILED" : "all ok");
	test_guard();
	printf("guard pages (no read past the end of a string, no write past the end of a destination): %s\n", fails ? "FAILED" : "all ok");
	{
		char *big = malloc(1100), *dst = malloc(2200);
		size_t i;
		for (i = 0; i < 1000; i++) big[i] = "abcdef"[i % 6];
		big[1000] = 0;
		BENCH("strlcpy 1000 B", 1000, sinkv += strlcpy(dst, big, 2200));
		BENCH("strcat 1000 B", 1000, (dst[0] = 0, strcat(dst, big)));
		BENCH("strspn 1000 B (set abc..f)", 1000, sinkv += strspn(big, "abcdef"));
		BENCH("strcspn 1000 B (set xyz)", 1000, sinkv += strcspn(big, "xyz"));
		BENCH("strpbrk 1000 B (not found)", 1000, sinkv += (size_t)strpbrk(big, "xyz"));
		if (p_memccpy) BENCH("memccpy 1000 B (not found)", 1000, sinkv += (size_t)p_memccpy(dst, big, 'q', 1000));
		BENCH("strncpy 1000 B", 1000, strncpy(dst, big, 1000));
		BENCH("strstr 1000 B (not found)", 1000, sinkv += (size_t)strstr(big, "abcdeg"));
		BENCH("memmem 1000 B (not found)", 1000, sinkv += (size_t)memmem(big, 1000, "abcdeg", 6));
		BENCH("strcasecmp 1000 B (equal)", 1000, sinkv += (size_t)strcasecmp(big, big));
	}
	return fails != 0;
}
