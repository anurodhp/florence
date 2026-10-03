/*
 * Florence: two libc/libm functions Duktape (the optional JavaScript engine) calls that the iokit port's
 * libraries do not export: log2() (libsystem_m is a curated subset) and strptime() (no dylib has it).
 * Only built into JavaScript builds. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only
 */
#ifdef FLO_WITH_JS
#include <math.h>
#include <time.h>

/* log2 from log(), which Duktape already needs. Exact for powers of two (Math.log2(8) must be 3, and
 * log(x) * LOG2E is not always): a near-integer result is checked by building 2^n and comparing. */
double log2(double x)
{
	double r, p = 1.0, b = 2.0;
	long n, k;

	if (x != x || x < 0.0)
		return x - x == 0.0 ? 0.0 / 0.0 : x;     /* NaN in, NaN out; negative: NaN */
	if (x == 0.0)
		return -1.0 / 0.0;
	if (x - x != 0.0)
		return x;                                 /* +infinity */
	r = log(x) * 1.4426950408889634074;          /* log2(e) */
	n = (long)(r < 0.0 ? r - 0.5 : r + 0.5);
	if (r - (double)n < 1e-9 && (double)n - r < 1e-9 && n > -1074 && n < 1024) {
		for (k = n < 0 ? -n : n; k > 0; k >>= 1, b *= b)
			if (k & 1)
				p *= b;
		if ((n >= 0 ? p : 1.0 / p) == x)
			return (double)n;
	}
	return r;
}

/* "No parsing": the same answer duk_config.h gives for platforms without strptime (its Orbis branch
 * defines no DUK_USE_DATE_PRS_STRPTIME, "no parsing (not an error)"). duk_bi_date_parse_string_strptime()
 * in duktape.c treats NULL as "not recognised" and Date.parse() keeps its built-in ISO 8601 and
 * toString()/toUTCString() parsers, which are all Duktape promises to support. */
char *strptime(const char *s, const char *fmt, struct tm *tm)
{
	(void)s;
	(void)fmt;
	(void)tm;
	return NULL;
}
#endif
