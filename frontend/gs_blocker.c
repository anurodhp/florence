/*
 * Florence: a content blocker that reads Safari's content-blocker rule lists (the WebKit JSON
 * format: an array of {"trigger": {...}, "action": {...}}) and refuses matching network requests.
 *
 * Supported triggers: url-filter (a regular expression, searched anywhere in the URL),
 * url-filter-is-case-sensitive, resource-type, load-type (first-party / third-party),
 * if-domain / unless-domain (against the page that asked; a leading "*" also matches subdomains),
 * if-top-url / unless-top-url. Supported actions: block, ignore-previous-rules (cancels the blocks of
 * earlier rules that matched, as in WebKit), css-display-none (rules that apply everywhere are
 * written to an adblock.css which NetSurf's "hide ads" option loads). Not supported, and skipped:
 * block-cookies, make-https, if-frame-url and the rest.
 *
 * NetSurf 3.11 offers a frontend no hook that sees a request before it starts, so
 * scripts/build_netsurf.sh adds one call to its fetch_start() in the build copy:
 * flo_fetch_blocked(url, referrer). The "page" a request belongs to is its referrer.
 * The resource type is guessed from the URL's extension (the fetch layer does not know it).
 *
 * Built for a small machine: rules are indexed by a four-character piece of their pattern, so a
 * request only looks at the few rules that could match, never at the whole list.
 * The regular-expression matcher is the small backtracking one below: no libc regex needed.
 *
 * Parsing and compiling a big list costs seconds and tens of megabytes on a Pi, so the compiled rules
 * (flat records, regex programs and strings in one arena, plus the index) are saved to a cache file and
 * the next start maps it read-only: no parsing, no heap, and the kernel can drop its pages under memory
 * pressure. Everything the matcher reads is an offset into that arena, bounds-checked, so a damaged
 * cache cannot do worse than miss a rule.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only
 */
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gnustep/gs.h"

#define MAX_RULES 250000
#define MAX_CSS_SELECTORS 4000
#define BUCKETS 131072
#define STEP_BUDGET 20000

/* ---- a small regular-expression matcher (WebKit's url-filter subset, plus groups and |) ---- */

enum { OP_CHAR, OP_ANY, OP_CLASS, OP_SPLIT, OP_JMP, OP_BOL, OP_EOL, OP_MATCH };
struct inst { unsigned char op, c; short x, y; };      /* 6 bytes: a pattern is at most a few thousand */
struct re {
	struct inst *code;
	int n;
	unsigned char (*cls)[32];
	int ncls;
};

enum { N_EMPTY, N_CHAR, N_ANY, N_CLASS, N_BOL, N_EOL, N_SEQ, N_ALT, N_STAR, N_PLUS, N_OPT };
struct node { int type, cls; unsigned char c; struct node *a, *b; };

struct rc {                     /* regex compiler state */
	const char *p;
	bool icase, bad;
	struct node *root;
	unsigned char (*cls)[32];
	int ncls, capcls;
	struct inst *code;
	int n, cap;
	int depth;
};

static struct node *mk(struct rc *c, int type, struct node *a, struct node *b)
{
	struct node *n = calloc(1, sizeof(*n));

	if (n == NULL) {
		c->bad = true;
		return NULL;
	}
	n->type = type;
	n->a = a;
	n->b = b;
	return n;
}

static void free_node(struct node *n)
{
	if (n == NULL)
		return;
	free_node(n->a);
	free_node(n->b);
	free(n);
}

static int new_class(struct rc *c)
{
	if (c->ncls == c->capcls) {
		int cap = c->capcls ? c->capcls * 2 : 4;
		void *p = realloc(c->cls, (size_t)cap * 32);

		if (p == NULL) {
			c->bad = true;
			return 0;
		}
		c->cls = p;
		c->capcls = cap;
	}
	memset(c->cls[c->ncls], 0, 32);
	return c->ncls++;
}

static void cls_set(unsigned char *bm, int ch, bool icase)
{
	bm[(unsigned char)ch >> 3] |= (unsigned char)(1u << ((unsigned char)ch & 7));
	if (icase && isalpha(ch)) {
		int o = islower(ch) ? toupper(ch) : tolower(ch);

		bm[(unsigned char)o >> 3] |= (unsigned char)(1u << ((unsigned char)o & 7));
	}
}

static void cls_shorthand(unsigned char *bm, char k)
{
	int ch;

	for (ch = 1; ch < 256; ch++) {
		bool in = (k == 'd' && isdigit(ch)) || (k == 'w' && (isalnum(ch) || ch == '_')) ||
			  (k == 's' && isspace(ch));
		if (in)
			cls_set(bm, ch, false);
	}
}

static struct node *parse_alt(struct rc *c);

static struct node *parse_class(struct rc *c)
{
	struct node *n = mk(c, N_CLASS, NULL, NULL);
	bool neg = false, first = true;
	int idx, ch;

	if (n == NULL)
		return NULL;
	idx = new_class(c);
	n->cls = idx;
	if (*c->p == '^') {
		neg = true;
		c->p++;
	}
	while (*c->p != '\0' && (*c->p != ']' || first)) {
		int lo;

		first = false;
		if (*c->p == '\\' && c->p[1] != '\0') {
			c->p++;
			if (strchr("dws", *c->p) != NULL) {
				cls_shorthand(c->cls[idx], *c->p++);
				continue;
			}
		}
		lo = (unsigned char)*c->p++;
		if (*c->p == '-' && c->p[1] != '\0' && c->p[1] != ']') {
			int hi;

			c->p++;
			if (*c->p == '\\' && c->p[1] != '\0')
				c->p++;
			hi = (unsigned char)*c->p++;
			for (ch = lo; ch <= hi; ch++)
				cls_set(c->cls[idx], ch, c->icase);
		} else {
			cls_set(c->cls[idx], lo, c->icase);
		}
	}
	if (*c->p == ']')
		c->p++;
	if (neg) {
		for (ch = 0; ch < 32; ch++)
			c->cls[idx][ch] = (unsigned char)~c->cls[idx][ch];
	}
	return n;
}

static struct node *parse_atom(struct rc *c)
{
	struct node *n;
	char ch = *c->p;

	switch (ch) {
	case '(':
		c->p++;
		if (++c->depth > 32) {
			c->bad = true;
			return NULL;
		}
		n = parse_alt(c);
		c->depth--;
		if (*c->p == ')')
			c->p++;
		return n;
	case '[':
		c->p++;
		return parse_class(c);
	case '.':
		c->p++;
		return mk(c, N_ANY, NULL, NULL);
	case '^':
		c->p++;
		return mk(c, N_BOL, NULL, NULL);
	case '$':
		c->p++;
		return mk(c, N_EOL, NULL, NULL);
	case '\\':
		if (c->p[1] != '\0') {
			c->p++;
			ch = *c->p;
			if (strchr("dws", ch) != NULL) {
				c->p++;
				n = mk(c, N_CLASS, NULL, NULL);
				if (n != NULL) {
					n->cls = new_class(c);
					cls_shorthand(c->cls[n->cls], ch);
				}
				return n;
			}
		}
		/* an escaped character is itself: */
		/* fall through */
	default:
		c->p++;
		n = mk(c, N_CHAR, NULL, NULL);
		if (n != NULL)
			n->c = (unsigned char)(c->icase ? tolower((unsigned char)ch) : (unsigned char)ch);
		return n;
	}
}

static struct node *parse_seq(struct rc *c)
{
	struct node *seq = mk(c, N_EMPTY, NULL, NULL);

	while (!c->bad && *c->p != '\0' && *c->p != '|' && *c->p != ')') {
		struct node *atom = parse_atom(c);

		if (atom == NULL)
			break;
		if (*c->p == '*' || *c->p == '+' || *c->p == '?') {
			atom = mk(c, *c->p == '*' ? N_STAR : (*c->p == '+' ? N_PLUS : N_OPT), atom, NULL);
			c->p++;
		}
		seq = mk(c, N_SEQ, seq, atom);
	}
	return seq;
}

static struct node *parse_alt(struct rc *c)
{
	struct node *left = parse_seq(c);

	while (!c->bad && *c->p == '|') {
		c->p++;
		left = mk(c, N_ALT, left, parse_seq(c));
	}
	return left;
}

static int emit1(struct rc *c, int op, int ch, int x, int y)
{
	if (c->n >= 30000) {                    /* jump targets are 16 bits */
		c->bad = true;
		return 0;
	}
	if (c->n == c->cap) {
		int cap = c->cap ? c->cap * 2 : 32;
		void *p = realloc(c->code, (size_t)cap * sizeof(struct inst));

		if (p == NULL) {
			c->bad = true;
			return 0;
		}
		c->code = p;
		c->cap = cap;
	}
	c->code[c->n].op = (unsigned char)op;
	c->code[c->n].c = (unsigned char)ch;
	c->code[c->n].x = (short)x;
	c->code[c->n].y = (short)y;
	return c->n++;
}

static void emit(struct rc *c, struct node *n)
{
	int s, j, l;

	if (n == NULL || c->bad)
		return;
	switch (n->type) {
	case N_EMPTY: break;
	case N_CHAR: emit1(c, OP_CHAR, n->c, 0, 0); break;
	case N_ANY: emit1(c, OP_ANY, 0, 0, 0); break;
	case N_CLASS: emit1(c, OP_CLASS, 0, n->cls, 0); break;
	case N_BOL: emit1(c, OP_BOL, 0, 0, 0); break;
	case N_EOL: emit1(c, OP_EOL, 0, 0, 0); break;
	case N_SEQ: emit(c, n->a); emit(c, n->b); break;
	case N_ALT:
		s = emit1(c, OP_SPLIT, 0, 0, 0);
		c->code[s].x = (short)c->n;
		emit(c, n->a);
		j = emit1(c, OP_JMP, 0, 0, 0);
		if (!c->bad)
			c->code[s].y = (short)c->n;
		emit(c, n->b);
		if (!c->bad)
			c->code[j].x = (short)c->n;
		break;
	case N_STAR:
		s = emit1(c, OP_SPLIT, 0, 0, 0);
		if (!c->bad)
			c->code[s].x = (short)c->n;
		emit(c, n->a);
		emit1(c, OP_JMP, 0, s, 0);
		if (!c->bad)
			c->code[s].y = (short)c->n;
		break;
	case N_PLUS:
		l = c->n;
		emit(c, n->a);
		s = emit1(c, OP_SPLIT, 0, l, 0);
		if (!c->bad)
			c->code[s].y = (short)c->n;
		break;
	case N_OPT:
		s = emit1(c, OP_SPLIT, 0, 0, 0);
		if (!c->bad)
			c->code[s].x = (short)c->n;
		emit(c, n->a);
		if (!c->bad)
			c->code[s].y = (short)c->n;
		break;
	}
}

static void re_free(struct re *re)
{
	if (re != NULL) {
		free(re->code);
		free(re->cls);
		free(re);
	}
}

static struct re *re_compile(const char *pattern, bool icase)
{
	struct rc c;
	struct re *re;

	memset(&c, 0, sizeof(c));
	c.p = pattern;
	c.icase = icase;
	c.root = parse_alt(&c);
	if (!c.bad)
		emit(&c, c.root);
	if (!c.bad)
		emit1(&c, OP_MATCH, 0, 0, 0);
	free_node(c.root);
	if (c.bad || (re = calloc(1, sizeof(*re))) == NULL) {
		free(c.code);
		free(c.cls);
		return NULL;
	}
	re->code = c.code;
	re->n = c.n;
	re->cls = c.cls;
	re->ncls = c.ncls;
	return re;
}

static bool re_run(const struct re *re, int pc, const char *s, const char *start, int *budget)
{
	for (;;) {
		const struct inst *i;

		if (pc < 0 || pc >= re->n || --*budget < 0)       /* a program from a damaged cache cannot leave its array */
			return false;
		i = &re->code[pc];
		switch (i->op) {
		case OP_CHAR:
			if ((unsigned char)*s != i->c)
				return false;
			s++;
			pc++;
			break;
		case OP_ANY:
			if (*s == '\0')
				return false;
			s++;
			pc++;
			break;
		case OP_CLASS:
			if (*s == '\0' || i->x < 0 || i->x >= re->ncls || !(re->cls[i->x][(unsigned char)*s >> 3] & (1u << ((unsigned char)*s & 7))))
				return false;
			s++;
			pc++;
			break;
		case OP_BOL:
			if (s != start)
				return false;
			pc++;
			break;
		case OP_EOL:
			if (*s != '\0')
				return false;
			pc++;
			break;
		case OP_JMP:
			pc = i->x;
			break;
		case OP_SPLIT:
			if (re_run(re, i->x, s, start, budget))
				return true;
			pc = i->y;
			break;
		case OP_MATCH:
			return true;
		default:
			return false;
		}
	}
}

/* searched anywhere in s (an unanchored match, like WebKit's url-filter) */
static bool re_search(const struct re *re, const char *s)
{
	const char *p;
	int budget = STEP_BUDGET;

	for (p = s;; p++) {
		if (re_run(re, 0, p, s, &budget))
			return true;
		if (*p == '\0' || budget < 0)
			return false;
	}
}

/* test hooks */
bool flo_blocker_regex_test(const char *pattern, const char *subject, bool icase)
{
	struct re *re = re_compile(pattern, icase);
	char *low = NULL;
	bool r;
	size_t i;

	if (re == NULL)
		return false;
	if (icase) {
		low = strdup(subject);
		for (i = 0; low[i] != '\0'; i++)
			low[i] = (char)tolower((unsigned char)low[i]);
		subject = low;
	}
	r = re_search(re, subject);
	free(low);
	re_free(re);
	return r;
}

/* ---- rules ---------------------------------------------------------------------------- */

enum { RT_DOCUMENT = 1, RT_IMAGE = 2, RT_STYLE = 4, RT_SCRIPT = 8, RT_FONT = 16, RT_RAW = 32,
       RT_SVG = 64, RT_MEDIA = 128, RT_POPUP = 256, RT_ALL = 511 };
enum { ACT_BLOCK, ACT_IGNORE, ACT_CSS, ACT_OTHER };
enum { PARTY_ANY, PARTY_FIRST, PARTY_THIRD };

struct strlist { char **v; int n; };

struct rule {                   /* a rule while it is being read; add_rule() packs it into the arena */
	struct re *re;                  /* url-filter, NULL: matches every URL */
	char *literal;                  /* a piece every match contains, lower case (for the index), or NULL */
	unsigned short types;
	unsigned char party, action;
	bool case_sensitive;
	struct strlist if_domain, unless_domain;
	struct re *if_top, *unless_top;
};

/* the packed form the matcher uses; every uint32_t is an offset into the arena (0: none) */
struct rrec {
	uint32_t re, literal, if_domain, unless_domain, if_top, unless_top;
	int32_t next;                   /* in the index bucket's chain */
	uint16_t types;
	uint8_t party, action, case_sensitive, pad[3];
};

static struct { unsigned char *p; size_t len, cap; } ar;        /* the arena being built (heap) */
static struct rrec *recs;
static int nrules, caprules;
static const unsigned char *blob;       /* the arena the matcher reads: ar.p, or inside the mapped cache */
static size_t blob_len;
static int head[BUCKETS + 1];           /* [BUCKETS] is the "always look" chain */
static const int *headp = head;
static void *map_base;                  /* the cache mapping, if that is where the rules live */
static size_t map_len;
static unsigned *stamp;
static unsigned gen;
static bool enabled = true;
static int nfiles;
static unsigned long blocked_total;
static char *css_buf;
static size_t css_len;
static int css_count;
static bool indexed;

static void strlist_free(struct strlist *l)
{
	int i;

	for (i = 0; i < l->n; i++)
		free(l->v[i]);
	free(l->v);
	l->v = NULL;
	l->n = 0;
}

static void rule_free(struct rule *r)
{
	re_free(r->re);
	re_free(r->if_top);
	re_free(r->unless_top);
	free(r->literal);
	strlist_free(&r->if_domain);
	strlist_free(&r->unless_domain);
	memset(r, 0, sizeof(*r));
}

/* ---- the arena ---- */

static uint32_t ar_put(const void *d, size_t n)
{
	size_t at;

	if (ar.p == NULL) {
		ar.cap = 1 << 16;
		if ((ar.p = calloc(1, ar.cap)) == NULL) {
			ar.cap = 0;
			return 0;
		}
		ar.len = 8;                     /* offset 0 means "none" */
	}
	at = (ar.len + 3) & ~(size_t)3;
	if (at + n + 1 > ar.cap) {
		size_t cap = ar.cap * 2;
		unsigned char *np;

		while (cap < at + n + 1)
			cap *= 2;
		if ((np = realloc(ar.p, cap)) == NULL)
			return 0;
		memset(np + ar.cap, 0, cap - ar.cap);
		ar.p = np;
		ar.cap = cap;
	}
	memcpy(ar.p + at, d, n);
	memset(ar.p + at + n, 0, 1);
	ar.len = at + n;
	return at > UINT32_MAX ? 0 : (uint32_t)at;
}

static uint32_t pack_str(const char *s) { return ar_put(s, strlen(s) + 1); }

static uint32_t pack_re(const struct re *re)
{
	size_t csz = (size_t)re->ncls * 32, isz = (size_t)re->n * sizeof(struct inst);
	unsigned char *t = malloc(8 + csz + isz);
	int32_t h[2] = { re->n, re->ncls };
	uint32_t off;

	if (t == NULL)
		return 0;
	memcpy(t, h, 8);
	if (csz > 0)
		memcpy(t + 8, re->cls, csz);
	memcpy(t + 8 + csz, re->code, isz);
	off = ar_put(t, 8 + csz + isz);
	free(t);
	return off;
}

static uint32_t pack_list(const struct strlist *l)
{
	uint32_t *t;
	uint32_t off = 0;
	int i;

	if (l->n == 0)
		return 0;
	if ((t = malloc(((size_t)l->n + 1) * 4)) == NULL)
		return 0;
	t[0] = (uint32_t)l->n;
	for (i = 0; i < l->n; i++)
		if ((t[i + 1] = pack_str(l->v[i])) == 0)
			goto out;
	off = ar_put(t, ((size_t)l->n + 1) * 4);
out:
	free(t);
	return off;
}

/* reading the arena: never trusts an offset */
static bool re_at(uint32_t off, struct re *v)
{
	const int32_t *h;
	int32_t n, nc;

	if (off == 0 || blob_len < 16 || off > blob_len - 8)
		return false;
	h = (const int32_t *)(const void *)(blob + off);
	n = h[0];
	nc = h[1];
	if (n <= 0 || nc < 0 || (size_t)off + 8 + (size_t)nc * 32 + (size_t)n * sizeof(struct inst) > blob_len)
		return false;
	v->n = n;
	v->ncls = nc;
	v->cls = (unsigned char (*)[32])(void *)(blob + off + 8);
	v->code = (struct inst *)(void *)(blob + off + 8 + (size_t)nc * 32);
	return true;
}

static bool re_hit(uint32_t off, const char *subject)
{
	struct re v;

	return re_at(off, &v) && re_search(&v, subject);
}

static const char *str_at(uint32_t off)
{
	return off != 0 && off < blob_len ? (const char *)(blob + off) : NULL;
}

static bool domain_listed(uint32_t off, const char *host);

static void release_state(void)
{
	free(ar.p);
	memset(&ar, 0, sizeof(ar));
	if (map_base != NULL)
		munmap(map_base, map_len);
	else
		free(recs);
	map_base = NULL;
	map_len = 0;
	recs = NULL;
	free(stamp);
	free(css_buf);
	stamp = NULL;
	css_buf = NULL;
	css_len = 0;
	css_count = 0;
	nrules = caprules = 0;
	nfiles = 0;
	indexed = false;
	blob = NULL;
	blob_len = 0;
	headp = head;
}

void flo_blocker_clear(void)
{
	release_state();
	memset(head, 0xff, sizeof(head));
}

/* a piece of the pattern every match must contain: the longest run of plain characters that are
 * not optional, repeated, grouped or alternatives. NULL when there is none (or it is under 4 chars) */
static char *required_literal(const char *pat)
{
	char run[256], best[256];
	size_t rl = 0, bl = 0;
	const char *p;
	int depth;

	if (strchr(pat, '|') != NULL)
		return NULL;
	run[0] = best[0] = '\0';
	for (p = pat; *p != '\0'; p++) {
		bool plain = false;
		char ch = *p;

		if (ch == '\\' && p[1] != '\0' && strchr("dwsDWS", p[1]) == NULL) {
			ch = *++p;
			plain = true;
		} else if (strchr("^$.*+?[](){}|\\", ch) == NULL) {
			plain = true;
		}
		if (plain && (p[1] == '*' || p[1] == '?'))
			plain = false;                  /* optional: not required */
		if (plain && p[1] == '+') {             /* required once, but the run ends here */
			if (rl < sizeof(run) - 1)
				run[rl++] = (char)tolower((unsigned char)ch);
			run[rl] = '\0';
			if (rl > bl) {
				memcpy(best, run, rl + 1);
				bl = rl;
			}
			rl = 0;
			continue;
		}
		if (plain) {
			if (rl < sizeof(run) - 1)
				run[rl++] = (char)tolower((unsigned char)ch);
			run[rl] = '\0';
			continue;
		}
		if (rl > bl) {
			memcpy(best, run, rl + 1);
			bl = rl;
		}
		rl = 0;
		run[0] = '\0';
		if (ch == '[') {                        /* skip the class */
			while (p[1] != '\0' && p[1] != ']')
				p++;
			if (p[1] == ']')
				p++;
		} else if (ch == '(') {                 /* skip the group: it may be optional */
			depth = 1;
			while (p[1] != '\0' && depth > 0) {
				p++;
				if (*p == '(')
					depth++;
				else if (*p == ')')
					depth--;
			}
		}
	}
	if (rl > bl) {
		memcpy(best, run, rl + 1);
		bl = rl;
	}
	return bl >= 4 ? strdup(best) : NULL;
}

static unsigned gram_hash(const char *p)
{
	unsigned h = 2166136261u;
	int i;

	for (i = 0; i < 4; i++)
		h = (h ^ (unsigned char)p[i]) * 16777619u;
	return h % BUCKETS;
}

/* the least ordinary four-character piece of the literal: it files the rule in the index */
static const char *pick_gram(const char *lit)
{
	static const char *const common[] = { "http", "ttps", "://", "www.", ".com", ".net", ".org", "html", "/ads", NULL };
	const char *best = NULL, *p;
	int best_score = -1, k;

	for (p = lit; p[3] != '\0'; p++) {
		int score = 0;
		bool is_common = false;

		for (k = 0; k < 4; k++)
			score += isalnum((unsigned char)p[k]) ? 2 : 0;
		for (k = 0; common[k] != NULL; k++)
			if (strncmp(p, common[k], strlen(common[k]) < 4 ? strlen(common[k]) : 4) == 0 && strlen(common[k]) >= 4)
				is_common = true;
		if (is_common)
			score -= 5;
		if (score > best_score) {
			best_score = score;
			best = p;
		}
	}
	return best;
}

/* ---- JSON ------------------------------------------------------------------------------- */

struct jp { const char *p, *end; bool bad; };

static void ws(struct jp *j)
{
	while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r'))
		j->p++;
}

static bool lit_(struct jp *j, char ch)
{
	ws(j);
	if (j->p < j->end && *j->p == ch) {
		j->p++;
		return true;
	}
	return false;
}

static void put_utf8(char *out, size_t *n, unsigned cp)
{
	if (cp < 0x80) {
		out[(*n)++] = (char)cp;
	} else if (cp < 0x800) {
		out[(*n)++] = (char)(0xC0 | (cp >> 6));
		out[(*n)++] = (char)(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		out[(*n)++] = (char)(0xE0 | (cp >> 12));
		out[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[(*n)++] = (char)(0x80 | (cp & 0x3F));
	} else {
		out[(*n)++] = (char)(0xF0 | (cp >> 18));
		out[(*n)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
		out[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[(*n)++] = (char)(0x80 | (cp & 0x3F));
	}
}

/* a string value: malloc'd and decoded; NULL (and j->bad) when malformed */
static char *jstr(struct jp *j)
{
	const char *s;
	char *out;
	size_t n = 0;

	ws(j);
	if (j->p >= j->end || *j->p != '"') {
		j->bad = true;
		return NULL;
	}
	j->p++;
	s = j->p;
	while (s < j->end && *s != '"') {
		if (*s == '\\')
			s++;
		s++;
	}
	if (s >= j->end) {
		j->bad = true;
		return NULL;
	}
	out = malloc((size_t)(s - j->p) + 8);
	if (out == NULL) {
		j->bad = true;
		return NULL;
	}
	while (j->p < s) {
		if (*j->p != '\\') {
			out[n++] = *j->p++;
			continue;
		}
		j->p++;
		switch (*j->p) {
		case 'n': out[n++] = '\n'; break;
		case 't': out[n++] = '\t'; break;
		case 'r': out[n++] = '\r'; break;
		case 'b': out[n++] = '\b'; break;
		case 'f': out[n++] = '\f'; break;
		case 'u': {
			unsigned cp = 0;
			int k;

			for (k = 1; k <= 4 && j->p + k < s; k++)
				cp = cp * 16 + (unsigned)(isdigit((unsigned char)j->p[k]) ? j->p[k] - '0' :
					(tolower((unsigned char)j->p[k]) - 'a' + 10) & 15);
			j->p += 4;
			if (cp >= 0xD800 && cp < 0xDC00 && j->p + 6 < s && j->p[1] == '\\' && j->p[2] == 'u') {
				unsigned lo = 0;

				for (k = 3; k <= 6; k++)
					lo = lo * 16 + (unsigned)(isdigit((unsigned char)j->p[k]) ? j->p[k] - '0' :
						(tolower((unsigned char)j->p[k]) - 'a' + 10) & 15);
				cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
				j->p += 6;
			}
			put_utf8(out, &n, cp);
			break;
		}
		default: out[n++] = *j->p; break;       /* \" \\ \/ */
		}
		j->p++;
	}
	j->p = s + 1;
	out[n] = '\0';
	return out;
}

static void jskip(struct jp *j, int depth);

static void jskip_string(struct jp *j) { free(jstr(j)); }

static void jskip(struct jp *j, int depth)
{
	ws(j);
	if (j->p >= j->end || depth > 64) {
		j->bad = true;
		return;
	}
	if (*j->p == '"') {
		jskip_string(j);
	} else if (*j->p == '{' || *j->p == '[') {
		char close = *j->p == '{' ? '}' : ']';

		j->p++;
		ws(j);
		if (j->p < j->end && *j->p == close) {
			j->p++;
			return;
		}
		while (!j->bad) {
			if (close == '}') {
				jskip_string(j);
				if (!lit_(j, ':'))
					j->bad = true;
			}
			jskip(j, depth + 1);
			if (lit_(j, ','))
				continue;
			if (!lit_(j, close))
				j->bad = true;
			break;
		}
	} else {                                        /* number, true, false, null */
		while (j->p < j->end && !strchr(",]} \t\r\n", *j->p))
			j->p++;
	}
}

static bool jbool(struct jp *j)
{
	ws(j);
	if (j->end - j->p >= 4 && strncmp(j->p, "true", 4) == 0) {
		j->p += 4;
		return true;
	}
	jskip(j, 0);
	return false;
}

static void jstrlist(struct jp *j, struct strlist *out)
{
	int cap = 0;

	if (!lit_(j, '[')) {
		jskip(j, 0);
		return;
	}
	if (lit_(j, ']'))
		return;
	for (;;) {
		char *s = jstr(j);

		if (j->bad)
			return;
		if (out->n == cap) {
			char **v;

			cap = cap ? cap * 2 : 4;
			v = realloc(out->v, (size_t)cap * sizeof(char *));
			if (v == NULL) {
				free(s);
				j->bad = true;
				return;
			}
			out->v = v;
		}
		for (char *q = s; *q != '\0'; q++)
			*q = (char)tolower((unsigned char)*q);
		out->v[out->n++] = s;
		if (lit_(j, ','))
			continue;
		if (!lit_(j, ']'))
			j->bad = true;
		return;
	}
}

static unsigned type_bit(const char *t)
{
	static const struct { const char *name; unsigned bit; } map[] = {
		{ "document", RT_DOCUMENT }, { "image", RT_IMAGE }, { "style-sheet", RT_STYLE },
		{ "script", RT_SCRIPT }, { "font", RT_FONT }, { "raw", RT_RAW }, { "svg-document", RT_SVG },
		{ "media", RT_MEDIA }, { "popup", RT_POPUP } };
	size_t i;

	for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (strcmp(t, map[i].name) == 0)
			return map[i].bit;
	return 0;
}

struct pending {                        /* one rule being read */
	char *filter, *selector, *action_type;
	bool case_sensitive, have_trigger;
	struct strlist rtypes, ltypes, if_top, unless_top;
	struct rule r;
};

static void pending_free(struct pending *p)
{
	free(p->filter);
	free(p->selector);
	free(p->action_type);
	strlist_free(&p->rtypes);
	strlist_free(&p->ltypes);
	strlist_free(&p->if_top);
	strlist_free(&p->unless_top);
	strlist_free(&p->r.if_domain);
	strlist_free(&p->r.unless_domain);
}

static void parse_trigger(struct jp *j, struct pending *p)
{
	if (!lit_(j, '{')) {
		jskip(j, 0);
		return;
	}
	p->have_trigger = true;
	if (lit_(j, '}'))
		return;
	for (;;) {
		char *key = jstr(j);

		if (j->bad)
			return;
		if (!lit_(j, ':')) {
			free(key);
			j->bad = true;
			return;
		}
		if (strcmp(key, "url-filter") == 0) {
			free(p->filter);
			p->filter = jstr(j);
		} else if (strcmp(key, "url-filter-is-case-sensitive") == 0) {
			p->case_sensitive = jbool(j);
		} else if (strcmp(key, "resource-type") == 0) {
			jstrlist(j, &p->rtypes);
		} else if (strcmp(key, "load-type") == 0) {
			jstrlist(j, &p->ltypes);
		} else if (strcmp(key, "if-domain") == 0) {
			jstrlist(j, &p->r.if_domain);
		} else if (strcmp(key, "unless-domain") == 0) {
			jstrlist(j, &p->r.unless_domain);
		} else if (strcmp(key, "if-top-url") == 0) {
			jstrlist(j, &p->if_top);
		} else if (strcmp(key, "unless-top-url") == 0) {
			jstrlist(j, &p->unless_top);
		} else {
			jskip(j, 0);
		}
		free(key);
		if (j->bad)
			return;
		if (lit_(j, ','))
			continue;
		if (!lit_(j, '}'))
			j->bad = true;
		return;
	}
}

static void parse_action(struct jp *j, struct pending *p)
{
	if (!lit_(j, '{')) {
		jskip(j, 0);
		return;
	}
	if (lit_(j, '}'))
		return;
	for (;;) {
		char *key = jstr(j);

		if (j->bad)
			return;
		if (!lit_(j, ':')) {
			free(key);
			j->bad = true;
			return;
		}
		if (strcmp(key, "type") == 0) {
			free(p->action_type);
			p->action_type = jstr(j);
		} else if (strcmp(key, "selector") == 0) {
			free(p->selector);
			p->selector = jstr(j);
		} else {
			jskip(j, 0);
		}
		free(key);
		if (j->bad)
			return;
		if (lit_(j, ','))
			continue;
		if (!lit_(j, '}'))
			j->bad = true;
		return;
	}
}

/* an OR of the patterns in a list, compiled as one expression */
static struct re *compile_any(const struct strlist *l, bool icase)
{
	size_t len = 1;
	char *pat, *q;
	struct re *re;
	int i;

	if (l->n == 0)
		return NULL;
	for (i = 0; i < l->n; i++)
		len += strlen(l->v[i]) + 3;
	pat = malloc(len);
	if (pat == NULL)
		return NULL;
	q = pat;
	for (i = 0; i < l->n; i++)
		q += sprintf(q, "%s(%s)", i ? "|" : "", l->v[i]);
	re = re_compile(pat, icase);
	free(pat);
	return re;
}

static bool add_rule(struct pending *p)
{
	struct rule *r = &p->r;
	int i;

	if (!p->have_trigger || p->action_type == NULL)
		return false;
	if (strcmp(p->action_type, "block") == 0) {
		r->action = ACT_BLOCK;
	} else if (strcmp(p->action_type, "ignore-previous-rules") == 0) {
		r->action = ACT_IGNORE;
	} else if (strcmp(p->action_type, "css-display-none") == 0) {
		r->action = ACT_CSS;
	} else {
		return false;                           /* block-cookies, make-https ...: not supported */
	}
	r->case_sensitive = p->case_sensitive;
	r->types = 0;
	for (i = 0; i < p->rtypes.n; i++)
		r->types |= (unsigned short)type_bit(p->rtypes.v[i]);
	if (p->rtypes.n == 0 || r->types == 0)
		r->types = RT_ALL;
	r->party = PARTY_ANY;
	for (i = 0; i < p->ltypes.n; i++) {
		if (strcmp(p->ltypes.v[i], "first-party") == 0)
			r->party = r->party == PARTY_THIRD ? PARTY_ANY : PARTY_FIRST;
		else if (strcmp(p->ltypes.v[i], "third-party") == 0)
			r->party = r->party == PARTY_FIRST ? PARTY_ANY : PARTY_THIRD;
	}

	if (r->action == ACT_CSS) {
		/* page styling cannot depend on a URL: only rules that apply everywhere can become CSS */
		bool generic = r->if_domain.n == 0 && r->unless_domain.n == 0 && r->types == RT_ALL &&
			       r->party == PARTY_ANY && p->if_top.n == 0 && p->unless_top.n == 0 &&
			       (p->filter == NULL || strcmp(p->filter, ".*") == 0 || p->filter[0] == '\0');
		if (generic && p->selector != NULL && css_count < MAX_CSS_SELECTORS) {
			size_t sl = strlen(p->selector);
			char *nb = realloc(css_buf, css_len + sl + 3);

			if (nb != NULL) {
				css_buf = nb;
				memcpy(css_buf + css_len, p->selector, sl);
				css_len += sl;
				css_buf[css_len++] = '\n';
				css_buf[css_len] = '\0';
				css_count++;
			}
		}
		return false;                           /* not a request rule */
	}

	if (p->filter != NULL && strcmp(p->filter, ".*") != 0 && p->filter[0] != '\0') {
		r->re = re_compile(p->filter, !p->case_sensitive);
		if (r->re == NULL)
			return false;                   /* a pattern we cannot read: drop the rule, not the list */
		r->literal = required_literal(p->filter);
	}
	r->if_top = compile_any(&p->if_top, true);
	r->unless_top = compile_any(&p->unless_top, true);

	{
		struct rrec rec;

		memset(&rec, 0, sizeof(rec));
		rec.types = r->types;
		rec.party = r->party;
		rec.action = r->action;
		rec.case_sensitive = r->case_sensitive;
		rec.next = -1;
		if ((r->re != NULL && (rec.re = pack_re(r->re)) == 0) ||
		    (r->literal != NULL && (rec.literal = pack_str(r->literal)) == 0) ||
		    (r->if_domain.n > 0 && (rec.if_domain = pack_list(&r->if_domain)) == 0) ||
		    (r->unless_domain.n > 0 && (rec.unless_domain = pack_list(&r->unless_domain)) == 0) ||
		    (r->if_top != NULL && (rec.if_top = pack_re(r->if_top)) == 0) ||
		    (r->unless_top != NULL && (rec.unless_top = pack_re(r->unless_top)) == 0)) {
			rule_free(r);
			return false;
		}
		rule_free(r);                           /* what was built is in the arena now */
		if (nrules == caprules) {
			int cap = caprules ? caprules * 2 : 1024;
			struct rrec *nr = realloc(recs, (size_t)cap * sizeof(*recs));

			if (nr == NULL)
				return false;
			recs = nr;
			caprules = cap;
		}
		recs[nrules++] = rec;
	}
	return true;
}

/* returns the number of rules added, or -1 when the file cannot be read as a rule list */
int flo_blocker_load_text(const char *text, size_t len)
{
	struct jp j = { text, text + len, false };
	int added = 0;

	indexed = false;
	if (!lit_(&j, '['))
		return -1;
	if (lit_(&j, ']'))
		return 0;
	while (!j.bad && nrules < MAX_RULES) {
		struct pending p;

		memset(&p, 0, sizeof(p));
		if (!lit_(&j, '{')) {
			j.bad = true;
			break;
		}
		if (!lit_(&j, '}')) {
			for (;;) {
				char *key = jstr(&j);

				if (j.bad)
					break;
				if (!lit_(&j, ':')) {
					free(key);
					j.bad = true;
					break;
				}
				if (strcmp(key, "trigger") == 0)
					parse_trigger(&j, &p);
				else if (strcmp(key, "action") == 0)
					parse_action(&j, &p);
				else
					jskip(&j, 0);
				free(key);
				if (j.bad)
					break;
				if (lit_(&j, ','))
					continue;
				if (!lit_(&j, '}'))
					j.bad = true;
				break;
			}
		}
		if (!j.bad && add_rule(&p))
			added++;
		pending_free(&p);
		if (j.bad)
			break;
		if (lit_(&j, ','))
			continue;
		if (!lit_(&j, ']'))
			j.bad = true;
		break;
	}
	return added;                                   /* what was read before any malformed part */
}

int flo_blocker_load_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	long size;
	char *buf;
	int n;

	if (f == NULL)
		return -1;
	if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || size > 64L * 1024 * 1024) {
		fclose(f);
		return -1;
	}
	rewind(f);
	buf = malloc((size_t)size + 1);
	if (buf == NULL || fread(buf, 1, (size_t)size, f) != (size_t)size) {
		free(buf);
		fclose(f);
		return -1;
	}
	fclose(f);
	buf[size] = '\0';
	n = flo_blocker_load_text(buf, (size_t)size);
	free(buf);
	if (n >= 0)
		nfiles++;
	return n;
}

#define MAX_SRC 64

/* the .json files of a directory, by name (so the order of rules does not depend on the file system) */
static int list_json(const char *dir, char names[][NAME_MAX + 1])
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int n = 0;

	if (d == NULL)
		return 0;
	while ((e = readdir(d)) != NULL && n < MAX_SRC) {
		size_t len = strlen(e->d_name);

		if (len >= 6 && len <= NAME_MAX && strcmp(e->d_name + len - 5, ".json") == 0)
			strcpy(names[n++], e->d_name);
	}
	closedir(d);
	qsort(names, (size_t)n, NAME_MAX + 1, (int (*)(const void *, const void *))strcmp);
	return n;
}

int flo_blocker_load_dir(const char *dir)
{
	static char names[MAX_SRC][NAME_MAX + 1];
	char path[PATH_MAX];
	int i, n = list_json(dir, names), loaded = 0;

	for (i = 0; i < n; i++) {
		snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
		if (flo_blocker_load_file(path) >= 0)
			loaded++;
	}
	return loaded;
}

/* files the loaded rules into the index; writes the stylesheet of the cosmetic rules, if asked */
void flo_blocker_finish(const char *css_path)
{
	int i;

	memset(head, 0xff, sizeof(head));
	headp = head;
	free(stamp);
	stamp = calloc((size_t)(nrules ? nrules : 1), sizeof(*stamp));
	gen = 0;
	blob = ar.p;                                    /* the arena stopped growing */
	blob_len = ar.p != NULL ? ar.len + 1 : 0;       /* ar_put keeps a zero byte after the end */
	for (i = nrules - 1; i >= 0; i--) {             /* so each chain runs in rule order */
		const char *lit = str_at(recs[i].literal);
		const char *g = lit != NULL ? pick_gram(lit) : NULL;
		unsigned b = g != NULL ? gram_hash(g) : BUCKETS;

		recs[i].next = head[b];
		head[b] = i;
	}
	indexed = true;

	if (css_path != NULL) {
		static const char marker[] = "/* generated by Florence from the content-blocker lists */";
		FILE *f;

		if (css_count > 0 && (f = fopen(css_path, "w")) != NULL) {
			char *p = css_buf, *nl;
			int k = 0;

			fprintf(f, "%s\n", marker);
			while ((nl = strchr(p, '\n')) != NULL) {
				fprintf(f, "%.*s%s", (int)(nl - p), p, (++k % 100 == 0) ? " { display: none !important }\n" : ",\n");
				p = nl + 1;
			}
			if (k % 100 != 0)
				fprintf(f, "html.florence-none { display: none !important }\n"); /* closes the selector list */
			fclose(f);
		} else if (css_count == 0) {
			/* no cosmetic rules: remove a stylesheet of ours left behind, never anyone else's */
			char first[96] = "";

			f = fopen(css_path, "r");
			if (f != NULL) {
				if (fgets(first, sizeof(first), f) != NULL && strncmp(first, marker, strlen(marker)) == 0) {
					fclose(f);
					remove(css_path);
				} else {
					fclose(f);
				}
			}
		}
	}
}

/* ---- deciding ---------------------------------------------------------------------------- */

static const char *host_of(const char *url, char *out, size_t len)
{
	const char *p = strstr(url, "://"), *e;
	size_t n;

	if (p == NULL)
		return NULL;
	p += 3;
	if ((e = strchr(p, '@')) != NULL && e < p + strcspn(p, "/?#"))
		p = e + 1;
	e = p;
	while (*e != '\0' && *e != '/' && *e != ':' && *e != '?' && *e != '#')
		e++;
	n = (size_t)(e - p);
	if (n == 0 || n >= len)
		return NULL;
	for (len = 0; len < n; len++)
		out[len] = (char)tolower((unsigned char)p[len]);
	out[n] = '\0';
	return out;
}

/* the part of a host that names the site: example.com, bbc.co.uk */
static const char *site_of(const char *host)
{
	const char *last = strrchr(host, '.'), *prev, *p;

	if (last == NULL)
		return host;
	for (prev = last - 1; prev > host && *prev != '.'; prev--)
		;
	if (prev <= host)
		return host;                            /* two labels already */
	p = prev + 1;                                   /* the second-level label */
	if (strlen(last + 1) == 2 && (last - p == 2 || last - p == 3) &&
	    (strncmp(p, "co", 2) == 0 || strncmp(p, "com", 3) == 0 || strncmp(p, "org", 3) == 0 ||
	     strncmp(p, "net", 3) == 0 || strncmp(p, "gov", 3) == 0 || strncmp(p, "edu", 3) == 0 ||
	     strncmp(p, "ac", 2) == 0)) {
		const char *pp;

		for (pp = prev - 1; pp > host && *pp != '.'; pp--)
			;
		return pp > host ? pp + 1 : host;       /* three labels */
	}
	return p;
}

static unsigned guess_type(const char *url)
{
	const char *q = url + strcspn(url, "?#"), *dot = NULL, *p;
	char ext[8];
	size_t n;
	static const struct { const char *e; unsigned t; } map[] = {
		{ "js", RT_SCRIPT }, { "mjs", RT_SCRIPT }, { "css", RT_STYLE },
		{ "png", RT_IMAGE }, { "jpg", RT_IMAGE }, { "jpeg", RT_IMAGE }, { "gif", RT_IMAGE },
		{ "webp", RT_IMAGE }, { "ico", RT_IMAGE }, { "bmp", RT_IMAGE }, { "svg", RT_SVG | RT_IMAGE },
		{ "woff", RT_FONT }, { "woff2", RT_FONT }, { "ttf", RT_FONT }, { "otf", RT_FONT }, { "eot", RT_FONT },
		{ "mp4", RT_MEDIA }, { "webm", RT_MEDIA }, { "mp3", RT_MEDIA }, { "ogg", RT_MEDIA },
		{ "m3u8", RT_MEDIA }, { "html", RT_DOCUMENT }, { "htm", RT_DOCUMENT }, { "json", RT_RAW } };
	size_t i;

	for (p = url + (strstr(url, "://") ? (size_t)(strstr(url, "://") - url) + 3 : 0); p < q; p++) {
		if (*p == '/')
			dot = NULL;
		else if (*p == '.')
			dot = p;
	}
	if (dot == NULL || (n = (size_t)(q - dot - 1)) == 0 || n >= sizeof(ext))
		return RT_DOCUMENT | RT_RAW | RT_SCRIPT | RT_IMAGE;     /* cannot tell: be inclusive */
	for (i = 0; i < n; i++)
		ext[i] = (char)tolower((unsigned char)dot[1 + i]);
	ext[n] = '\0';
	for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (strcmp(ext, map[i].e) == 0)
			return map[i].t;
	return RT_DOCUMENT | RT_RAW | RT_SCRIPT | RT_IMAGE;
}

static bool domain_listed(uint32_t off, const char *host)
{
	const uint32_t *l;
	uint32_t i, n;
	size_t hl = strlen(host);

	if (off == 0 || off > blob_len - 4)
		return false;
	l = (const uint32_t *)(const void *)(blob + off);
	n = l[0];
	if ((size_t)off + 4 + (size_t)n * 4 > blob_len)
		return false;
	for (i = 0; i < n; i++) {
		const char *d = str_at(l[1 + i]);
		size_t dl;

		if (d == NULL)
			continue;
		if (*d == '*') {                        /* the domain and everything under it */
			d++;
			dl = strlen(d);
			if (hl == dl && strcmp(host, d) == 0)
				return true;
			if (hl > dl && strcmp(host + hl - dl, d) == 0 && (d[0] == '.' || host[hl - dl - 1] == '.'))
				return true;
		} else if (strcmp(host, d) == 0) {
			return true;
		}
	}
	return false;
}

static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

bool flo_fetch_blocked(const char *url, const char *referrer)
{
	char low[2048], host[256], phost[256], pagelow[1024];
	const char *h, *ph = NULL;
	int cand_buf[512], *cand = cand_buf, ncand = 0, capcand = 512, i, r, steps;
	unsigned types;
	size_t len;
	bool third = false, unknown = true, blocked = false, heap = false;

	if (!enabled || nrules == 0 || !indexed || blob == NULL || url == NULL || strncmp(url, "http", 4) != 0)
		return false;
	for (len = 0; url[len] != '\0' && len < sizeof(low) - 1; len++)
		low[len] = (char)tolower((unsigned char)url[len]);
	low[len] = '\0';
	h = host_of(url, host, sizeof(host));
	if (h == NULL)
		return false;
	if (referrer != NULL && (ph = host_of(referrer, phost, sizeof(phost))) != NULL) {
		unknown = false;
		third = strcmp(site_of(host), site_of(phost)) != 0;
	}
	types = guess_type(url);
	if (referrer != NULL) {
		for (i = 0; referrer[i] != '\0' && i < (int)sizeof(pagelow) - 1; i++)
			pagelow[i] = (char)tolower((unsigned char)referrer[i]);
		pagelow[i] = '\0';
	} else {
		pagelow[0] = '\0';
	}

	/* the candidates: the rules filed under any four-character piece of this URL, and the rules
	 * that have none; each once, then in rule order */
	if (++gen == 0)
		memset(stamp, 0, (size_t)nrules * sizeof(*stamp)), gen = 1;
#define ADD(chain) for (r = (chain), steps = 0; r >= 0 && r < nrules && steps++ < nrules; r = recs[r].next) { \
		if (stamp[r] == gen) continue; \
		stamp[r] = gen; \
		if (ncand == capcand) { \
			int *nc = heap ? realloc(cand, (size_t)capcand * 2 * sizeof(int)) : malloc((size_t)capcand * 2 * sizeof(int)); \
			if (nc == NULL) break; \
			if (!heap) memcpy(nc, cand, (size_t)ncand * sizeof(int)); \
			cand = nc; capcand *= 2; heap = true; \
		} \
		cand[ncand++] = r; }
	for (i = 0; low[i] != '\0' && low[i + 1] != '\0' && low[i + 2] != '\0' && low[i + 3] != '\0'; i++)
		ADD(headp[gram_hash(low + i)])
	ADD(headp[BUCKETS])
#undef ADD
	qsort(cand, (size_t)ncand, sizeof(int), cmp_int);

	for (i = 0; i < ncand; i++) {
		const struct rrec *ru = &recs[cand[i]];
		const char *lit;

		if (!(ru->types & types))
			continue;
		if (ru->party != PARTY_ANY && (unknown || (ru->party == PARTY_THIRD) != third))
			continue;
		if ((lit = str_at(ru->literal)) != NULL && strstr(low, lit) == NULL)
			continue;
		if (ru->re != 0 && !re_hit(ru->re, ru->case_sensitive ? url : low))
			continue;
		if (ru->if_domain != 0 && (ph == NULL || !domain_listed(ru->if_domain, ph)))
			continue;
		if (ru->unless_domain != 0 && ph != NULL && domain_listed(ru->unless_domain, ph))
			continue;
		if (ru->if_top != 0 && (referrer == NULL || !re_hit(ru->if_top, pagelow)))
			continue;
		if (ru->unless_top != 0 && referrer != NULL && re_hit(ru->unless_top, pagelow))
			continue;
		if (ru->action == ACT_BLOCK)
			blocked = true;
		else if (ru->action == ACT_IGNORE)
			blocked = false;
	}
	if (heap)
		free(cand);
	if (blocked)
		blocked_total++;
	return blocked;
}

/* ---- the compiled cache -------------------------------------------------------------------- */

#define CACHE_VERSION 1
struct chdr {
	char magic[8];
	uint32_t version, probe, rrec_size, buckets, nrules, css_count, nfiles, pad;
	uint64_t sig, off_head, off_recs, off_blob, blob_len, total;
};

static uint64_t fnv(uint64_t h, const void *d, size_t n)
{
	const unsigned char *p = d;

	while (n-- > 0)
		h = (h ^ *p++) * 1099511628211ull;
	return h;
}

static bool cache_write(const char *path, uint64_t sig)
{
	char tmp[PATH_MAX + 8];
	static const char zeros[8] = { 0 };
	struct chdr h;
	size_t head_sz = (BUCKETS + 1) * sizeof(int), recs_sz = (size_t)nrules * sizeof(struct rrec);
	FILE *f;
	bool ok;

	if (ar.p == NULL || snprintf(tmp, sizeof(tmp), "%s.new", path) >= (int)sizeof(tmp))
		return false;
	memset(&h, 0, sizeof(h));
	memcpy(h.magic, "FLOBLK\0\1", 8);
	h.version = CACHE_VERSION;
	h.probe = 0x01020304u;
	h.rrec_size = sizeof(struct rrec);
	h.buckets = BUCKETS;
	h.nrules = (uint32_t)nrules;
	h.css_count = (uint32_t)css_count;
	h.nfiles = (uint32_t)nfiles;
	h.sig = sig;
	h.off_head = (sizeof(h) + 7) & ~(size_t)7;
	h.off_recs = (h.off_head + head_sz + 7) & ~(size_t)7;
	h.off_blob = (h.off_recs + recs_sz + 7) & ~(size_t)7;
	h.blob_len = ar.len + 1;
	h.total = h.off_blob + h.blob_len;
	if ((f = fopen(tmp, "wb")) == NULL)
		return false;
	ok = fwrite(&h, sizeof(h), 1, f) == 1;
	ok = ok && fwrite(zeros, 1, h.off_head - sizeof(h), f) == h.off_head - sizeof(h);
	ok = ok && fwrite(head, 1, head_sz, f) == head_sz;
	ok = ok && fwrite(zeros, 1, h.off_recs - h.off_head - head_sz, f) == h.off_recs - h.off_head - head_sz;
	ok = ok && fwrite(recs, 1, recs_sz, f) == recs_sz;
	ok = ok && fwrite(zeros, 1, h.off_blob - h.off_recs - recs_sz, f) == h.off_blob - h.off_recs - recs_sz;
	ok = ok && fwrite(ar.p, 1, h.blob_len, f) == h.blob_len;
	if (fclose(f) != 0)
		ok = false;
	if (!ok || rename(tmp, path) != 0) {
		remove(tmp);
		return false;
	}
	return true;
}

/* maps the cache and makes it the active rule set; false (nothing changed) if it is missing, stale or damaged */
static bool cache_map(const char *path, uint64_t sig, const char *css_path)
{
	struct chdr h;
	struct stat st;
	unsigned char *m;
	int fd = open(path, O_RDONLY);

	if (fd < 0)
		return false;
	if (fstat(fd, &st) != 0 || st.st_size < (off_t)sizeof(h)) {
		close(fd);
		return false;
	}
	m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (m == MAP_FAILED)
		return false;
	memcpy(&h, m, sizeof(h));
	if (memcmp(h.magic, "FLOBLK\0\1", 8) != 0 || h.version != CACHE_VERSION || h.probe != 0x01020304u ||
	    h.rrec_size != sizeof(struct rrec) || h.buckets != BUCKETS || h.sig != sig ||
	    h.total != (uint64_t)st.st_size || h.nrules == 0 || h.nrules > MAX_RULES || h.blob_len < 16 ||
	    h.off_head % 8 != 0 || h.off_recs % 8 != 0 || h.off_blob % 8 != 0 ||
	    h.off_head + (BUCKETS + 1) * sizeof(int) > h.off_recs ||
	    h.off_recs + (uint64_t)h.nrules * sizeof(struct rrec) > h.off_blob ||
	    h.off_blob + h.blob_len != h.total ||
	    (h.css_count > 0 && (css_path == NULL || access(css_path, R_OK) != 0))) {
		munmap(m, (size_t)st.st_size);
		return false;
	}
	release_state();
	map_base = m;
	map_len = (size_t)st.st_size;
	headp = (const int *)(const void *)(m + h.off_head);
	recs = (struct rrec *)(void *)(m + h.off_recs);
	blob = m + h.off_blob;
	blob_len = (size_t)h.blob_len;
	nrules = (int)h.nrules;
	css_count = (int)h.css_count;
	nfiles = (int)h.nfiles;
	stamp = calloc((size_t)nrules, sizeof(*stamp));
	gen = 0;
	indexed = stamp != NULL;
	return indexed;
}

int flo_blocker_setup(const char *default_list, const char *dir, const char *css_path, const char *cache_path)
{
	static char names[MAX_SRC][NAME_MAX + 1];
	char path[MAX_SRC + 1][PATH_MAX];
	struct stat st;
	uint64_t sig = 1469598103934665603ull;
	int i, n = 0, nn, loaded = 0;

	if (default_list != NULL && stat(default_list, &st) == 0)
		snprintf(path[n++], PATH_MAX, "%s", default_list);
	nn = dir != NULL ? list_json(dir, names) : 0;
	for (i = 0; i < nn; i++)
		snprintf(path[n++], PATH_MAX, "%s/%s", dir, names[i]);
	sig = fnv(sig, "florence-blocker", 16);
	sig = fnv(sig, &(uint32_t){ CACHE_VERSION }, 4);
	for (i = 0; i < n; i++) {
		long long size = 0, mtime = 0;

		if (stat(path[i], &st) == 0) {
			size = (long long)st.st_size;
			mtime = (long long)st.st_mtime;
		}
		sig = fnv(sig, path[i], strlen(path[i]) + 1);
		sig = fnv(sig, &size, sizeof(size));
		sig = fnv(sig, &mtime, sizeof(mtime));
	}

	flo_blocker_clear();
	if (n > 0 && cache_path != NULL && cache_map(cache_path, sig, css_path))
		return nrules;                          /* the compiled rules, as they were: nothing to parse */
	for (i = 0; i < n; i++)
		if (flo_blocker_load_file(path[i]) >= 0)
			loaded++;
	flo_blocker_finish(css_path);
	if (loaded > 0 && cache_path != NULL && nrules > 0 && cache_write(cache_path, sig))
		cache_map(cache_path, sig, css_path);   /* drop the heap copy for the mapped one; on failure keep it */
	return nrules;
}

/* ---- status ------------------------------------------------------------------------------- */

bool flo_blocker_enabled(void) { return enabled; }
void flo_blocker_enable(bool on) { enabled = on; }
int flo_blocker_rule_count(void) { return nrules; }
int flo_blocker_file_count(void) { return nfiles; }
int flo_blocker_css_count(void) { return css_count; }
unsigned long flo_blocker_blocked_count(void) { return blocked_total; }
