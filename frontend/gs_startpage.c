/*
 * Florence: the start page and favicons. A new tab or window without an address opens a local
 * page (the file generated here, shown by NetSurf itself): a row of tiles for the bookmarks and,
 * below it, a row of recently visited sites, like Safari's start page. Each tile is a rounded
 * picture made with cairo: the site's favicon when one has been seen, else a coloured tile with
 * the site's initial. Favicons are saved as the core delivers them (set_icon) in
 * $HOME/.netsurf/favicons/<host>.png.
 * The lists are read from the files the UI keeps (~/.netsurf/Bookmarks and History, one
 * "address<TAB>title" per line), so this file needs nothing from the Objective-C side.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cairo.h>

#include "utils/errors.h"
#include "utils/nsurl.h"
#include "netsurf/browser_window.h"
#include "netsurf/content.h"

#include "gnustep/gs.h"

#define TILES 8                 /* per row */
#define TILE_PX 96
#define LINE_MAX_LEN 4096

struct site {
	char url[LINE_MAX_LEN];
	char title[256];
	char host[256];
};

static char start_dir[PATH_MAX], icon_dir[PATH_MAX], page_url[PATH_MAX + 16];

static const char *home(void)
{
	const char *h = getenv("HOME");
	return h != NULL ? h : "/tmp";
}

static void ensure_dirs(void)
{
	char d[PATH_MAX];

	snprintf(d, sizeof(d), "%s/.netsurf", home());
	mkdir(d, 0755);
	snprintf(start_dir, sizeof(start_dir), "%s/.netsurf/start", home());
	mkdir(start_dir, 0755);
	snprintf(icon_dir, sizeof(icon_dir), "%s/.netsurf/favicons", home());
	mkdir(icon_dir, 0755);
}

/* "https://www.example.com:8080/x" -> "example.com"; false if there is no host */
static bool host_of(const char *url, char *out, size_t len)
{
	const char *p = strstr(url, "://"), *e;
	size_t n;

	if (p == NULL)
		return false;
	p += 3;
	if (strncmp(p, "www.", 4) == 0)
		p += 4;
	e = p;
	while (*e != '\0' && *e != '/' && *e != ':' && *e != '?' && *e != '#')
		e++;
	n = (size_t)(e - p);
	if (n == 0 || n >= len)
		return false;
	memcpy(out, p, n);
	out[n] = '\0';
	for (n = 0; out[n] != '\0'; n++)
		out[n] = (char)tolower((unsigned char)out[n]);
	return true;
}

/* a host as a file name: letters, digits, dot and dash only */
static void safe_name(const char *host, char *out, size_t len)
{
	size_t n = 0;

	for (; *host != '\0' && n + 1 < len; host++)
		out[n++] = (isalnum((unsigned char)*host) || *host == '.' || *host == '-') ? *host : '_';
	out[n] = '\0';
}

static unsigned hash_of(const char *s)
{
	unsigned h = 5381;

	while (*s != '\0')
		h = h * 33 + (unsigned char)*s++;
	return h;
}

static void icon_path(const char *host, char *out, size_t len)
{
	char name[300];

	safe_name(host, name, sizeof(name));
	snprintf(out, len, "%s/%s.png", icon_dir, name);
}

/* ---- favicons ------------------------------------------------------------------ */

/* Saves `icon` (any size, ARGB) as the site's favicon, scaled to fit 64x64. */
bool flo_favicon_store(const char *host, cairo_surface_t *icon)
{
	cairo_surface_t *out;
	cairo_t *cr;
	char path[PATH_MAX], tmp[PATH_MAX + 8];
	double w, h, s;
	bool ok;

	if (host == NULL || *host == '\0' || icon == NULL || cairo_surface_status(icon) != CAIRO_STATUS_SUCCESS)
		return false;
	w = cairo_image_surface_get_width(icon);
	h = cairo_image_surface_get_height(icon);
	if (w < 1 || h < 1)
		return false;
	ensure_dirs();
	out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64);
	cr = cairo_create(out);
	s = 64.0 / (w > h ? w : h);
	if (s > 4.0)
		s = 4.0;
	cairo_translate(cr, (64 - w * s) / 2, (64 - h * s) / 2);
	cairo_scale(cr, s, s);
	cairo_set_source_surface(cr, icon, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_destroy(cr);
	icon_path(host, path, sizeof(path));
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	ok = cairo_surface_write_to_png(out, tmp) == CAIRO_STATUS_SUCCESS && rename(tmp, path) == 0;
	cairo_surface_destroy(out);
	return ok;
}

/* called from the window table's set_icon: the core's favicon for the page now shown in bw */
void flo_favicon_save(struct browser_window *bw, struct hlcache_handle *icon)
{
	char *host_s = NULL, host[256];
	size_t len = 0;
	struct bitmap *bm = icon != NULL ? content_get_bitmap(icon) : NULL;
	nsurl *u = bw != NULL ? browser_window_access_url(bw) : NULL;

	if (bm == NULL || u == NULL)
		return;
	if (nsurl_get(u, NSURL_SCHEME | NSURL_HOST, &host_s, &len) != NSERROR_OK || host_s == NULL)
		return;
	if (host_of(host_s, host, sizeof(host)))
		flo_favicon_store(host, (cairo_surface_t *)flo_bitmap_surface(bm));
	free(host_s);
}

/* ---- tiles -------------------------------------------------------------------- */

static void rounded(cairo_t *cr, double x, double y, double w, double h, double r)
{
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
	cairo_close_path(cr);
}

static bool make_tile(const char *path, const char *host)
{
	static const double palette[][3] = {
		{ 0.85, 0.27, 0.27 }, { 0.90, 0.55, 0.15 }, { 0.20, 0.62, 0.45 }, { 0.20, 0.52, 0.80 },
		{ 0.45, 0.38, 0.80 }, { 0.78, 0.30, 0.55 }, { 0.35, 0.45, 0.55 }, { 0.55, 0.50, 0.30 },
	};
	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, TILE_PX, TILE_PX);
	cairo_t *cr = cairo_create(surf);
	char ipath[PATH_MAX];
	cairo_surface_t *fav = NULL;
	bool ok;

	icon_path(host, ipath, sizeof(ipath));
	if (access(ipath, R_OK) == 0) {
		fav = cairo_image_surface_create_from_png(ipath);
		if (cairo_surface_status(fav) != CAIRO_STATUS_SUCCESS) {
			cairo_surface_destroy(fav);
			fav = NULL;
		}
	}
	rounded(cr, 1, 1, TILE_PX - 2, TILE_PX - 2, 20);
	if (fav != NULL) {
		cairo_set_source_rgb(cr, 1, 1, 1);
		cairo_fill_preserve(cr);
		cairo_set_source_rgb(cr, 0.80, 0.80, 0.80);
		cairo_set_line_width(cr, 1.5);
		cairo_stroke(cr);
		cairo_save(cr);
		cairo_translate(cr, (TILE_PX - 56) / 2.0, (TILE_PX - 56) / 2.0);
		cairo_scale(cr, 56.0 / cairo_image_surface_get_width(fav), 56.0 / cairo_image_surface_get_height(fav));
		cairo_set_source_surface(cr, fav, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint(cr);
		cairo_restore(cr);
		cairo_surface_destroy(fav);
	} else {
		const double *c = palette[hash_of(host) % 8];
		char letter[2] = { (char)toupper((unsigned char)host[0]), '\0' };
		cairo_text_extents_t e;

		cairo_set_source_rgb(cr, c[0], c[1], c[2]);
		cairo_fill(cr);
		cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, 48);
		cairo_text_extents(cr, letter, &e);
		cairo_set_source_rgb(cr, 1, 1, 1);
		cairo_move_to(cr, (TILE_PX - e.width) / 2 - e.x_bearing, (TILE_PX - e.height) / 2 - e.y_bearing);
		cairo_show_text(cr, letter);
	}
	cairo_destroy(cr);
	ok = cairo_surface_write_to_png(surf, path) == CAIRO_STATUS_SUCCESS;
	cairo_surface_destroy(surf);
	return ok;
}

/* ---- the page ----------------------------------------------------------------- */

static void esc(FILE *f, const char *s)
{
	for (; *s != '\0'; s++) {
		switch (*s) {
		case '&': fputs("&amp;", f); break;
		case '<': fputs("&lt;", f); break;
		case '>': fputs("&gt;", f); break;
		case '"': fputs("&quot;", f); break;
		default: fputc(*s, f);
		}
	}
}

/* up to `max` entries of a store file; history keeps one entry (the newest) per host */
static int read_sites(const char *file, struct site *out, int max, bool one_per_host, const struct site *skip, int nskip)
{
	char path[PATH_MAX], line[LINE_MAX_LEN + 300];
	FILE *f;
	int n = 0, i;

	snprintf(path, sizeof(path), "%s/.netsurf/%s", home(), file);
	if ((f = fopen(path, "r")) == NULL)
		return 0;
	while (n < max && fgets(line, sizeof(line), f) != NULL) {
		struct site s;
		char *tab, *nl;
		bool dup = false;

		if ((nl = strpbrk(line, "\r\n")) != NULL)
			*nl = '\0';
		tab = strchr(line, '\t');
		if (tab != NULL)
			*tab++ = '\0';
		snprintf(s.url, sizeof(s.url), "%s", line);
		snprintf(s.title, sizeof(s.title), "%s", tab != NULL ? tab : "");
		if (!host_of(s.url, s.host, sizeof(s.host)))
			continue;
		if (strncmp(s.url, "http://", 7) != 0 && strncmp(s.url, "https://", 8) != 0)
			continue;
		for (i = 0; i < nskip && !dup; i++)
			dup = strcmp(skip[i].host, s.host) == 0;
		for (i = 0; i < n && one_per_host && !dup; i++)
			dup = strcmp(out[i].host, s.host) == 0;
		if (dup)
			continue;
		if (one_per_host) {     /* a history entry stands for its site: link the site's front door */
			const char *p = strstr(s.url, "://") + 3;
			const char *slash = strchr(p, '/');
			size_t keep = slash != NULL ? (size_t)(slash - s.url) : strlen(s.url);

			snprintf(s.title, sizeof(s.title), "%s", s.host);
			s.url[keep < sizeof(s.url) - 2 ? keep : sizeof(s.url) - 2] = '\0';
			strcat(s.url, "/");
		}
		out[n++] = s;
	}
	fclose(f);
	return n;
}

/* at most `max` characters (UTF-8 aware), with "..." where it was cut */
static void ellipsize(const char *in, char *out, size_t outlen, int max)
{
	size_t i = 0, o = 0;
	int chars = 0;

	while (in[i] != '\0' && o + 5 < outlen) {
		if (((unsigned char)in[i] & 0xC0) != 0x80) {
			if (chars == max) {
				memcpy(out + o, "...", 3);
				o += 3;
				break;
			}
			chars++;
		}
		out[o++] = in[i++];
	}
	out[o] = '\0';
}

static void write_row(FILE *f, const struct site *sites, int n, const char *tile_prefix)
{
	int i;

	for (i = 0; i < n; i++) {
		char tile[PATH_MAX];
		const char *full = sites[i].title[0] != '\0' ? sites[i].title : sites[i].host;
		char label[300];

		ellipsize(full, label, sizeof(label), 15);

		snprintf(tile, sizeof(tile), "%s/%s%u.png", start_dir, tile_prefix, hash_of(sites[i].host));
		make_tile(tile, sites[i].host);
		fputs("<a class=\"tile\" href=\"", f);
		esc(f, sites[i].url);
		fprintf(f, "\"><img src=\"%s%u.png\" width=\"%d\" height=\"%d\" alt=\"\"><span>", tile_prefix,
			hash_of(sites[i].host), TILE_PX, TILE_PX);
		esc(f, label);
		fputs("</span></a>\n", f);
	}
}

/* Writes the start page and its tiles; returns its file: URL (valid until the next call). */
const char *flo_startpage_url(void)
{
	struct site *marks = calloc(TILES, sizeof(*marks)), *recent = calloc(TILES, sizeof(*recent));
	char path[PATH_MAX];
	FILE *f;
	int nm, nr;

	ensure_dirs();
	snprintf(path, sizeof(path), "%s/index.html", start_dir);
	snprintf(page_url, sizeof(page_url), "file://%s", path);
	if (marks == NULL || recent == NULL || (f = fopen(path, "w")) == NULL) {
		free(marks);
		free(recent);
		return NULL;
	}
	nm = read_sites("Bookmarks", marks, TILES, false, NULL, 0);
	/* history is newest first; skip sites already shown as bookmarks */
	nr = read_sites("History", recent, TILES, true, marks, nm);

	fputs("<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>Start Page</title>\n<style>\n"
	      "body { background: #f2f2f2; color: #333; font-family: sans-serif; margin: 0; padding: 36px 0; text-align: center; }\n"
	      "h1 { font-size: 22px; font-weight: normal; color: #555; margin: 0 0 8px 0; }\n"
	      "h2 { font-size: 14px; font-weight: bold; color: #777; margin: 34px 0 12px 0; }\n"
	      ".row { max-width: 1060px; margin: 0 auto; text-align: center; }\n"
	      "a.tile { display: inline-block; width: 112px; margin: 6px 7px; text-decoration: none; color: #333; vertical-align: top; }\n"
	      "a.tile img { border: 0; }\n"
	      "a.tile span { display: block; font-size: 12px; margin-top: 6px; height: 16px; overflow: hidden; white-space: nowrap; }\n"
	      ".empty { color: #999; font-size: 13px; margin: 18px 0; }\n"
	      "</style></head><body>\n<h1>Florence</h1>\n<h2>Bookmarks</h2>\n<div class=\"row\">\n", f);
	if (nm > 0)
		write_row(f, marks, nm, "b");
	else
		fputs("<p class=\"empty\">Press the star in the toolbar (or Cmd-D) to keep a site here.</p>\n", f);
	fputs("</div>\n<h2>Recently visited</h2>\n<div class=\"row\">\n", f);
	if (nr > 0)
		write_row(f, recent, nr, "h");
	else
		fputs("<p class=\"empty\">Sites you visit will appear here.</p>\n", f);
	fputs("</div>\n</body></html>\n", f);
	fclose(f);
	free(marks);
	free(recent);
	return page_url;
}
