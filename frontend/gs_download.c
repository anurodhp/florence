/*
 * Florence: NetSurf's download table. A response the browser cannot show is saved to
 * $HOME/Downloads under a safe, unused name; progress and the result go to the status line
 * of the tab that started it (if that tab is still open). The core hands over the data in
 * pieces, then calls done or error; after either, the context is ours to destroy.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "utils/errors.h"
#include "netsurf/download.h"
#include "desktop/download.h"

#include "gnustep/gs.h"

struct gui_download_window {
	struct gui_download_window *next;
	download_context *ctx;
	struct gui_window *gw;          /* the starting tab; NULL once it has been closed */
	FILE *fp;
	char path[PATH_MAX];
	char name[256];
	unsigned long long total, got;
	int last_pct;
	bool failed;
};

static struct gui_download_window *active;

static void say(struct gui_download_window *dw, const char *fmt, const char *a, const char *b)
{
	char msg[PATH_MAX + 300];

	snprintf(msg, sizeof(msg), fmt, a, b);
	fprintf(stderr, "florence: download: %s\n", msg);
	if (dw->gw != NULL && dw->gw->ui != NULL)
		flo_ui_set_status(dw->gw->ui, msg);
}

/* the tab was closed: its downloads carry on, silently */
void flo_download_forget(struct gui_window *gw)
{
	struct gui_download_window *d;

	for (d = active; d != NULL; d = d->next)
		if (d->gw == gw)
			d->gw = NULL;
}

/* a file name that is safe to create: no directories, no control characters, never empty */
static void clean_name(const char *in, char *out, size_t len)
{
	const char *base = in != NULL ? in : "";
	const char *slash;
	size_t n = 0;

	while ((slash = strpbrk(base, "/\\")) != NULL)
		base = slash + 1;
	for (; *base != '\0' && n + 1 < len; base++)
		out[n++] = ((unsigned char)*base < 32 || *base == 0x7f) ? '_' : *base;
	out[n] = '\0';
	if (n == 0 || strcmp(out, ".") == 0 || strcmp(out, "..") == 0)
		snprintf(out, len, "download");
	if (out[0] == '.')              /* no hidden files */
		out[0] = '_';
}

/* $HOME/Downloads/name, or name-1, name-2 ... before the extension if it exists */
static bool pick_path(struct gui_download_window *dw)
{
	const char *home = getenv("HOME");
	char dir[PATH_MAX], stem[256], ext[256];
	const char *dot;
	struct stat st;
	int i;
	const char *chosen = flo_pref_get("downloads", "");

	if (*chosen != '\0')
		snprintf(dir, sizeof(dir), "%s", chosen);
	else
		snprintf(dir, sizeof(dir), "%s/Downloads", home != NULL ? home : "/tmp");
	if (mkdir(dir, 0755) != 0 && errno != EEXIST)
		return false;
	dot = strrchr(dw->name, '.');
	if (dot != NULL && dot != dw->name) {
		snprintf(stem, sizeof(stem), "%.*s", (int)(dot - dw->name), dw->name);
		snprintf(ext, sizeof(ext), "%s", dot);
	} else {
		snprintf(stem, sizeof(stem), "%s", dw->name);
		ext[0] = '\0';
	}
	snprintf(dw->path, sizeof(dw->path), "%s/%.200s%.60s", dir, stem, ext);
	for (i = 1; lstat(dw->path, &st) == 0 && i < 1000; i++)
		snprintf(dw->path, sizeof(dw->path), "%s/%.200s-%d%.60s", dir, stem, i, ext);
	return i < 1000;
}

static struct gui_download_window *dl_create(download_context *ctx, struct gui_window *parent)
{
	struct gui_download_window *dw = calloc(1, sizeof(*dw));

	if (dw == NULL)
		return NULL;
	dw->ctx = ctx;
	dw->gw = parent;
	dw->total = download_context_get_total_length(ctx);
	dw->last_pct = -1;
	clean_name(download_context_get_filename(ctx), dw->name, sizeof(dw->name));
	if (!pick_path(dw) || (dw->fp = fopen(dw->path, "wb")) == NULL) {
		say(dw, "cannot save %s (%s)", dw->name, strerror(errno));
		dw->failed = true;      /* keep the download alive so the core can finish it; the data is dropped */
	} else {
		say(dw, "downloading %s%s", dw->name, "...");
	}
	dw->next = active;
	active = dw;
	return dw;
}

static nserror dl_data(struct gui_download_window *dw, const char *data, unsigned int size)
{
	int pct;

	if (dw->failed || dw->fp == NULL)
		return NSERROR_OK;      /* dropped; the error is reported when the download ends */
	if (fwrite(data, 1, size, dw->fp) != size) {
		dw->failed = true;
		fclose(dw->fp);
		dw->fp = NULL;
		remove(dw->path);
		return NSERROR_OK;
	}
	dw->got += size;
	pct = dw->total > 0 ? (int)(dw->got * 100 / dw->total) : (int)(dw->got >> 18);   /* unknown size: per 256 KB */
	if (pct != dw->last_pct) {
		char amount[48];

		dw->last_pct = pct;
		if (dw->total > 0)
			snprintf(amount, sizeof(amount), "%d%%", pct);
		else
			snprintf(amount, sizeof(amount), "%llu KB", dw->got >> 10);
		say(dw, "downloading %s: %s", dw->name, amount);
	}
	return NSERROR_OK;
}

static void dl_finish(struct gui_download_window *dw)
{
	struct gui_download_window **pp;

	for (pp = &active; *pp != NULL; pp = &(*pp)->next) {
		if (*pp == dw) {
			*pp = dw->next;
			break;
		}
	}
	download_context_destroy(dw->ctx);
	free(dw);
}

static void dl_done(struct gui_download_window *dw)
{
	if (dw->fp != NULL && fclose(dw->fp) != 0)
		dw->failed = true;
	dw->fp = NULL;
	if (dw->failed) {
		remove(dw->path);
		say(dw, "download of %s failed", dw->name, "");
	} else {
		say(dw, "saved %s", dw->path, "");
	}
	dl_finish(dw);
}

static void dl_error(struct gui_download_window *dw, const char *msg)
{
	if (dw->fp != NULL)
		fclose(dw->fp);
	dw->fp = NULL;
	if (dw->path[0] != '\0')
		remove(dw->path);       /* a partial file is worse than none */
	say(dw, "download of %s failed: %s", dw->name, msg != NULL ? msg : "error");
	dl_finish(dw);
}

static struct gui_download_table download_table = {
	.create = dl_create,
	.data = dl_data,
	.error = dl_error,
	.done = dl_done,
};

struct gui_download_table *flo_download_table = &download_table;
