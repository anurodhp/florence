/*
 * Florence: EasyList. Downloads the Adblock Plus list once a week (and on first run), converts it
 * to a Safari content-blocker JSON file in ~/.netsurf/blocklists/ and has the blocker reload it.
 *
 * The download and conversion run on one short-lived background thread, so the UI never waits;
 * the result is installed with a rename, so a failed or interrupted update leaves the old list.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>

#include "gnustep/gs.h"

#define LIST_URL "https://easylist.to/easylist/easylist.txt"
#define MAX_DOWNLOAD (24L * 1024 * 1024)
#define MAX_COSMETIC 1000       /* element-hiding selectors kept: each costs the page's style engine */
#define MAX_OUT_RULES 150000
#define MAX_LINE 4096
#define WEEK (7 * 24 * 3600)

/* ---- Adblock Plus syntax -> content-blocker JSON ------------------------ */

struct out {
	FILE *f;
	long n;
	long cosmetic;
};

static void jstr(FILE *f, const char *s)
{
	fputc('"', f);
	for (; *s != '\0'; s++) {
		if (*s == '"' || *s == '\\')
			fputc('\\', f);
		if ((unsigned char)*s >= 0x20)
			fputc(*s, f);
	}
	fputc('"', f);
}

static void emit_head(struct out *o)
{
	fputs(o->n++ == 0 ? "\n" : ",\n", o->f);
}

/* the pattern part of a filter as a url-filter regex; false if it is unusable */
static bool to_regex(const char *p, char *re, size_t cap)
{
	size_t n = 0, len = strlen(p), i = 0;
	bool any = false;

#define PUT(s) do { size_t l_ = strlen(s); if (n + l_ + 1 >= cap) return false; memcpy(re + n, s, l_); n += l_; } while (0)
	if (len >= 2 && p[0] == '|' && p[1] == '|') {
		PUT("^[a-z][a-z0-9+.-]*://([^/]*\\.)?");
		i = 2;
	} else if (len >= 1 && p[0] == '|') {
		PUT("^");
		i = 1;
	} else {
		while (i < len && p[i] == '*')          /* a leading * means nothing */
			i++;
	}
	for (; i < len; i++) {
		char c = p[i], one[3] = { c, '\0', '\0' };

		if (c == '*') {
			if (i + 1 == len)
				break;                  /* trailing * means nothing */
			PUT(".*");
		} else if (c == '^') {
			PUT("[^a-zA-Z0-9_.%-]");
			any = true;
		} else if (c == '|' && i + 1 == len) {
			PUT("$");
		} else if (strchr(".+?()[]{}\\|$", c) != NULL) {
			one[0] = '\\';
			one[1] = c;
			PUT(one);
			any = true;
		} else if (c == ' ' || (unsigned char)c < 0x21 || (unsigned char)c > 0x7e) {
			return false;
		} else {
			PUT(one);
			any = true;
		}
	}
#undef PUT
	re[n] = '\0';
	return any && n > 2;
}

struct opts {
	bool ok;                        /* only options we understand */
	bool third, first;
	bool match_case;
	const char *types[8];
	int ntypes;
	char *dom_if[16], *dom_unless[16];
	int nif, nunless;
	char *buf;
};

static void add_type(struct opts *o, const char *t)
{
	int i;

	for (i = 0; i < o->ntypes; i++)
		if (strcmp(o->types[i], t) == 0)
			return;
	if (o->ntypes < 8)
		o->types[o->ntypes++] = t;
}

static void parse_opts(char *s, struct opts *o)
{
	char *tok, *save = NULL;

	memset(o, 0, sizeof(*o));
	o->ok = true;
	for (tok = strtok_r(s, ",", &save); tok != NULL; tok = strtok_r(NULL, ",", &save)) {
		if (strcmp(tok, "third-party") == 0 || strcmp(tok, "3p") == 0) {
			o->third = true;
		} else if (strcmp(tok, "~third-party") == 0 || strcmp(tok, "~3p") == 0 ||
			   strcmp(tok, "first-party") == 0 || strcmp(tok, "1p") == 0) {
			o->first = true;
		} else if (strcmp(tok, "script") == 0) {
			add_type(o, "script");
		} else if (strcmp(tok, "image") == 0) {
			add_type(o, "image");
		} else if (strcmp(tok, "stylesheet") == 0 || strcmp(tok, "css") == 0) {
			add_type(o, "style-sheet");
		} else if (strcmp(tok, "font") == 0) {
			add_type(o, "font");
		} else if (strcmp(tok, "media") == 0) {
			add_type(o, "media");
		} else if (strcmp(tok, "xmlhttprequest") == 0 || strcmp(tok, "xhr") == 0 ||
			   strcmp(tok, "ping") == 0 || strcmp(tok, "object") == 0 || strcmp(tok, "other") == 0) {
			add_type(o, "raw");
		} else if (strcmp(tok, "subdocument") == 0 || strcmp(tok, "frame") == 0 || strcmp(tok, "document") == 0) {
			add_type(o, "document");
		} else if (strcmp(tok, "match-case") == 0) {
			o->match_case = true;
		} else if (strcmp(tok, "important") == 0 || strcmp(tok, "collapse") == 0 || strcmp(tok, "~collapse") == 0) {
			/* no effect here */
		} else if (strncmp(tok, "domain=", 7) == 0) {
			char *d, *dsave = NULL;

			for (d = strtok_r(tok + 7, "|", &dsave); d != NULL; d = strtok_r(NULL, "|", &dsave)) {
				if (*d == '~') {
					if (o->nunless < 16)
						o->dom_unless[o->nunless++] = d + 1;
				} else if (o->nif < 16) {
					o->dom_if[o->nif++] = d;
				}
			}
		} else {
			o->ok = false;          /* ~script, popup, csp=, redirect=, removeparam ...: leave the rule out */
		}
	}
}

static void put_domains(FILE *f, const char *key, char **d, int n)
{
	int i;

	fprintf(f, ",\"%s\":[", key);
	for (i = 0; i < n; i++) {
		if (i > 0)
			fputc(',', f);
		fputc('"', f);
		fputc('*', f);
		fputs(d[i], f);
		fputc('"', f);
	}
	fputc(']', f);
}

/* pass 0 writes the blocking and element-hiding rules, pass 1 the exceptions (which must come last) */
static void convert_line(struct out *o, char *line, int pass)
{
	char re[1024], *opt, *dollar;
	bool exception = false;
	struct opts op;

	if (*line == '\0' || *line == '!' || *line == '[')
		return;
	if (strstr(line, "#@#") != NULL || strstr(line, "#?#") != NULL || strstr(line, "#$#") != NULL ||
	    strstr(line, "#%#") != NULL || strstr(line, "$@$") != NULL)
		return;

	/* element hiding: only the ones for every site, and only plain selectors */
	if (strstr(line, "##") != NULL) {
		const char *sel = strstr(line, "##");
		const char *c;

		if (pass != 0 || sel != line || o->cosmetic >= MAX_COSMETIC)
			return;
		sel += 2;
		if (*sel == '\0')
			return;
		for (c = sel; *c != '\0'; c++)
			if (!(isalnum((unsigned char)*c) || strchr("#.-_[]=\"'~^$*> +,|", *c) != NULL))
				return;         /* no pseudo-classes: libcss need not understand them */
		emit_head(o);
		fputs("{\"trigger\":{\"url-filter\":\".*\"},\"action\":{\"type\":\"css-display-none\",\"selector\":", o->f);
		jstr(o->f, sel);
		fputs("}}", o->f);
		o->cosmetic++;
		return;
	}

	if (line[0] == '@' && line[1] == '@') {
		exception = true;
		line += 2;
	}
	if ((pass == 1) != exception)
		return;
	if (o->n >= MAX_OUT_RULES)
		return;

	memset(&op, 0, sizeof(op));
	op.ok = true;
	dollar = strrchr(line, '$');
	if (dollar != NULL && strchr(dollar, '/') == NULL) {
		*dollar = '\0';
		opt = dollar + 1;
		parse_opts(opt, &op);
		if (!op.ok)
			return;
	}
	if (line[0] == '/' && line[strlen(line) - 1] == '/')
		return;                         /* a regular expression written for another engine */
	if (!to_regex(line, re, sizeof(re)))
		return;
	if (op.third && op.first)
		return;

	emit_head(o);
	fputs("{\"trigger\":{\"url-filter\":", o->f);
	jstr(o->f, re);
	if (op.match_case)
		fputs(",\"url-filter-is-case-sensitive\":true", o->f);
	if (op.ntypes > 0) {
		int i;

		fputs(",\"resource-type\":[", o->f);
		for (i = 0; i < op.ntypes; i++)
			fprintf(o->f, "%s\"%s\"", i > 0 ? "," : "", op.types[i]);
		fputc(']', o->f);
	}
	if (op.third)
		fputs(",\"load-type\":[\"third-party\"]", o->f);
	else if (op.first)
		fputs(",\"load-type\":[\"first-party\"]", o->f);
	if (op.nif > 0)
		put_domains(o->f, "if-domain", op.dom_if, op.nif);
	else if (op.nunless > 0)
		put_domains(o->f, "unless-domain", op.dom_unless, op.nunless);
	fprintf(o->f, "},\"action\":{\"type\":\"%s\"}}", exception ? "ignore-previous-rules" : "block");
}

/* rules written, or -1 */
long flo_abp_convert(const char *in, const char *out)
{
	struct out o = { NULL, 0, 0 };
	char line[MAX_LINE];
	int pass;
	FILE *f;

	if ((o.f = fopen(out, "w")) == NULL)
		return -1;
	fputc('[', o.f);
	for (pass = 0; pass < 2; pass++) {
		if ((f = fopen(in, "r")) == NULL) {
			fclose(o.f);
			return -1;
		}
		while (fgets(line, sizeof(line), f) != NULL) {
			size_t l = strlen(line);

			if (l == sizeof(line) - 1 && line[l - 1] != '\n') {      /* an absurd line: skip the rest of it */
				int c;
				while ((c = fgetc(f)) != EOF && c != '\n')
					;
				continue;
			}
			while (l > 0 && isspace((unsigned char)line[l - 1]))
				line[--l] = '\0';
			convert_line(&o, line, pass);
		}
		fclose(f);
	}
	fputs("\n]\n", o.f);
	if (fclose(o.f) != 0)
		return -1;
	return o.n;
}

/* ---- the weekly update ---------------------------------------------------- */

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static bool busy;
static int last_result;                 /* 0 none yet, 1 updated, -1 failed */
static char ca_path[PATH_MAX];
static char list_path[PATH_MAX], raw_path[PATH_MAX], part_path[PATH_MAX];

static size_t sink(char *p, size_t sz, size_t n, void *fp)
{
	return fwrite(p, sz, n, (FILE *)fp);
}

static bool download(const char *to)
{
	CURL *c = curl_easy_init();
	FILE *f;
	long code = 0;
	CURLcode rc;

	if (c == NULL || (f = fopen(to, "w")) == NULL) {
		if (c != NULL)
			curl_easy_cleanup(c);
		return false;
	}
	curl_easy_setopt(c, CURLOPT_URL, LIST_URL);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sink);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(c, CURLOPT_MAXFILESIZE, MAX_DOWNLOAD);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);    /* abandon a stalled transfer */
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);              /* we are not the main thread */
	curl_easy_setopt(c, CURLOPT_USERAGENT, "Florence");
	curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
	if (ca_path[0] != '\0')
		curl_easy_setopt(c, CURLOPT_CAINFO, ca_path);
	rc = curl_easy_perform(c);
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
	curl_easy_cleanup(c);
	if (fclose(f) != 0 || rc != CURLE_OK || code != 200)
		return false;
	return true;
}

static bool looks_like_list(const char *path)
{
	char head[16] = { 0 };
	struct stat st;
	FILE *f = fopen(path, "r");

	if (f == NULL)
		return false;
	if (fread(head, 1, 8, f) != 8)
		head[0] = '\0';
	fclose(f);
	return strncmp(head, "[Adblock", 8) == 0 && stat(path, &st) == 0 && st.st_size > 100 * 1024;
}

static void *worker(void *arg)
{
	int result = -1;
	long n;

	(void)arg;
	if (download(raw_path) && looks_like_list(raw_path) &&
	    (n = flo_abp_convert(raw_path, part_path)) > 1000 && rename(part_path, list_path) == 0)
		result = 1;
	unlink(raw_path);
	unlink(part_path);
	pthread_mutex_lock(&lock);
	last_result = result;
	busy = false;
	pthread_mutex_unlock(&lock);
	if (result > 0)
		flo_ui_lists_updated();         /* the main thread reloads the blocker */
	return NULL;
}

time_t flo_lists_updated(void)
{
	struct stat st;

	return list_path[0] != '\0' && stat(list_path, &st) == 0 ? st.st_mtime : 0;
}

bool flo_lists_busy(void)
{
	bool b;

	pthread_mutex_lock(&lock);
	b = busy;
	pthread_mutex_unlock(&lock);
	return b;
}

int flo_lists_last_result(void) { return last_result; }

/* start an update if one is due (or `force`); the background thread does the work */
void gs_lists_start(const char *home, const char *cabundle, bool force)
{
	pthread_t t;
	pthread_attr_t at;
	time_t when;

	snprintf(list_path, sizeof(list_path), "%s/.netsurf/blocklists/easylist.json", home);
	snprintf(raw_path, sizeof(raw_path), "%s/.netsurf/easylist.txt.new", home);
	snprintf(part_path, sizeof(part_path), "%s/.netsurf/easylist.json.new", home);
	snprintf(ca_path, sizeof(ca_path), "%s", cabundle != NULL ? cabundle : "");
	when = flo_lists_updated();
	if (!force && when != 0 && time(NULL) - when < WEEK)
		return;
	pthread_mutex_lock(&lock);
	if (busy) {
		pthread_mutex_unlock(&lock);
		return;
	}
	busy = true;
	pthread_mutex_unlock(&lock);
	pthread_attr_init(&at);
	pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize(&at, 512 * 1024);
	if (pthread_create(&t, &at, worker, NULL) != 0) {
		pthread_mutex_lock(&lock);
		busy = false;
		pthread_mutex_unlock(&lock);
	}
	pthread_attr_destroy(&at);
}
