/*
 * Florence: end-to-end check of the engine glue without a UI. Starts the engine, opens a page, pumps
 * GLib's main context exactly as FloGLib.m does (prepare, wait for what it asked for, dispatch), and checks:
 *   1. a frame arrives, and its pixels are right (a red box top left, a blue box lower)
 *   2. the title and address events fire
 *   3. a mouse click on a link goes through WPE's input path and loads the second page
 *   4. the wheel scrolls the page (the red box leaves the top of the frame)
 * Exit status 0 only if all hold. Built and run by scripts/test_host.sh.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#include "flo.h"

#include <poll.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

static int frames, title_seen_clicked, uri_events;
static char last_title[256], last_uri[512];
static int last_w, last_h;

static int trace_frames;
static void changed(void *ui, int x, int y, int w, int h) { (void)ui; frames++; if (trace_frames && frames % 20 == 0) printf("      frame %d: damage %dx%d at %d,%d\n", frames, w, h, x, y); }
static void title(void *ui, const char *t) { (void)ui; snprintf(last_title, sizeof last_title, "%s", t != NULL ? t : ""); if (strcmp(last_title, "Clicked") == 0) title_seen_clicked = 1; }
static void uri(void *ui, const char *u) { (void)ui; uri_events++; snprintf(last_uri, sizeof last_uri, "%s", u != NULL ? u : ""); }
static void progress(void *ui, double p) { (void)ui; (void)p; }
static void nav(void *ui, bool b, bool f) { (void)ui; (void)b; (void)f; }
static void hover(void *ui, const char *l) { (void)ui; (void)l; }

static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* one trip round the loop, the way FloGLib.m does it; returns after at most `max_ms` */
static void pump(int max_ms)
{
	struct flo_glib_wait w;
	struct pollfd p[FLO_GLIB_MAX_FDS];
	int i, t;

	if (!flo_glib_prepare(&w))
		return;
	for (i = 0; i < w.n; i++) {
		p[i].fd = w.fd[i];
		p[i].events = (w.read[i] ? POLLIN : 0) | (w.write[i] ? POLLOUT : 0);
		p[i].revents = 0;
	}
	t = w.timeout_ms < 0 || w.timeout_ms > max_ms ? max_ms : w.timeout_ms;
	poll(p, w.n, t);
	flo_glib_dispatch();
}

static bool pump_until(bool (*done)(void), double seconds)
{
	double end = now() + seconds;
	while (now() < end) {
		if (done())
			return true;
		pump(50);
	}
	return done();
}

static struct flo_page *page;
static int download_state = -1;
static void download_event(void *ctx, int state, const char *name, double fraction) { (void)ctx; (void)name; (void)fraction; download_state = state; }

static bool have_frame(void) { int w, h, s; return flo_page_pixels(page, &w, &h, &s) != NULL && frames > 0; }
static bool got_clicked(void) { return title_seen_clicked != 0; }

static int px(int x, int y, unsigned *rgb)
{
	int w, h, s;
	const uint8_t *p = flo_page_pixels(page, &w, &h, &s);
	if (p == NULL || x >= w || y >= h)
		return -1;
	p += (size_t)y * s + (size_t)x * 4;
	*rgb = (unsigned)p[2] << 16 | (unsigned)p[1] << 8 | p[0];
	last_w = w; last_h = h;
	return 0;
}

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } else { printf("ok:   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(int argc, char **argv)
{
	static const struct flo_page_events ev = { changed, title, uri, progress, nav, hover };
	char url[1024];
	unsigned c;

	setvbuf(stdout, NULL, _IONBF, 0);   /* progress is the point when the engine hangs (the first Pi run did) */
	if (argc < 3) {
		fprintf(stderr, "usage: %s first.html data-dir\n", argv[0]);
		return 2;
	}
	{
		char cache[1024];
		snprintf(cache, sizeof cache, "%s/cache", argv[2]);
		if (flo_engine_init(argv[2], cache) != 0) { printf("FAIL: engine init\n"); return 1; }
	}
	trace_frames = getenv("TRACE_FRAMES") != NULL;
	/* SMOKE_SCALE=20 on a Pi 3: the first load (libraries, font cache) takes far longer than the 3 s a PC needs */
	double scale = getenv("SMOKE_SCALE") ? atof(getenv("SMOKE_SCALE")) : 1.0;
	if (scale < 1.0) scale = 1.0;
	page = flo_page_new(&ev, NULL, 800, 600);
	/* a ready-made URL (about:, data:, http:) is used as is, a path becomes file:// */
	if (strchr(argv[1], ':') != NULL && argv[1][0] != '/')
		snprintf(url, sizeof url, "%s", argv[1]);
	else
		snprintf(url, sizeof url, "file://%s", argv[1]);
	flo_page_load(page, url);

	CHECK(pump_until(have_frame, 90), "a frame arrived (%d frames so far)", frames);
	/* let the page settle: the first frame can be the blank one */
	{ double end = now() + 3 * scale; while (now() < end && strcmp(last_title, "Smoke") != 0) pump(50); }
	CHECK(strcmp(last_title, "Smoke") == 0, "title event: \"%s\"", last_title);
	CHECK(strstr(last_uri, "first.html") != NULL, "address event: %s", last_uri);
	CHECK(px(20, 20, &c) == 0 && c == 0xff0000, "red box at (20,20): %06x in a %dx%d frame", c, last_w, last_h);
	CHECK(px(20, 420, &c) == 0 && c == 0x0000ff, "blue box at (20,420): %06x", c);
	CHECK(px(700, 20, &c) == 0 && c == 0xffffff, "white at (700,20): %06x", c);

	flo_page_pointer_move(page, 0, 350, 300);
	{ double end = now() + 0.3 * scale; while (now() < end) pump(20); }
	{ struct flo_hit hit; flo_page_hit(page, &hit); CHECK(hit.link != NULL && strstr(hit.link, "second.html") != NULL, "hit test under the pointer: link %s", hit.link ? hit.link : "(none)"); }
	flo_page_pointer_button(page, 0, 1, true, 1, 350, 300);
	flo_page_pointer_button(page, 0, 1, false, 1, 350, 300);
	CHECK(pump_until(got_clicked, 30 * scale), "a click on the link loaded the second page (title now \"%s\")", last_title);

	if (title_seen_clicked) {
		/* the second page is tall: a wheel notch must move it */
		unsigned before = 0, after = 0;
		{ double end = now() + 2; while (now() < end) pump(50); }
		int f0;
		px(20, 20, &before);
		f0 = frames;
		{ double end = now() + 3; while (now() < end) pump(50); }
		printf("      frames in 3 s of idle: %d\n", frames - f0);
		f0 = frames;
		flo_page_pointer_move(page, 0, 400, 300);
		{ struct timeval tv; gettimeofday(&tv, NULL); fprintf(stderr, "%ld.%03d ### wheel sent\n", (long)(tv.tv_sec % 1000), (int)(tv.tv_usec / 1000)); }
		flo_page_scroll(page, 0, 0, -5, 400, 300);   /* WPE/WebCore: negative is "down" */
		{ double end = now() + 3; while (now() < end) pump(50); }
		px(20, 20, &after);
		printf("      frames during the scroll: %d\n", frames - f0);
		if (getenv("TRY_KEYS")) {
			flo_page_special_key(page, 0, FLO_KEY_PAGE_DOWN, true);
			flo_page_special_key(page, 0, FLO_KEY_PAGE_DOWN, false);
			{ double end = now() + 2; while (now() < end) pump(50); }
			printf("      after Page Down:\n");
		}
		{ int y, first = -1, last = -1; unsigned v; for (y = 0; y < 600; y++) if (px(20, y, &v) == 0 && v == 0xff0000) { if (first < 0) first = y; last = y; } printf("      red rows in column 20: %d..%d\n", first, last); }
		{ int x, y, n = 0; unsigned v; for (x = last_w - 20; x < last_w; x++) for (y = 0; y < last_h; y++) if (px(x, y, &v) == 0 && v != 0xffffff) n++;
		  printf("      non-white pixels in the right-most 20 columns (a scroll bar): %d\n", n); }
		CHECK(before == 0xff0000 && after != 0xff0000, "wheel scrolled the tall page: top-left %06x -> %06x", before, after);
	}

	/* resize: the frame follows the viewport, in both directions */
	{
		int sizes[][2] = { { 1000, 700 }, { 640, 400 } }, k;
		double end;
		for (k = 0; k < 2; k++) {
			unsigned v;
			flo_page_resize(page, sizes[k][0], sizes[k][1]);
			end = now() + 8 * scale;
			while (now() < end) { pump(50); px(0, 0, &v); if (last_w == sizes[k][0] && last_h == sizes[k][1]) break; }
			CHECK(last_w == sizes[k][0] && last_h == sizes[k][1], "resize to %dx%d: frame is %dx%d", sizes[k][0], sizes[k][1], last_w, last_h);
		}
	}
	/* copy: select everything on the page and see the clipboard change */
	{
		int64_t c0 = flo_clipboard_count();
		double end;
		char *t;
		flo_page_focus(page, true);
		flo_page_edit(page, FLO_EDIT_SELECT_ALL);
		{ end = now() + 1; while (now() < end) pump(50); }
		flo_page_edit(page, FLO_EDIT_COPY);
		end = now() + 3 * scale;
		while (now() < end && flo_clipboard_count() == c0) pump(50);
		t = flo_clipboard_text();
		CHECK(flo_clipboard_count() > c0 && t != NULL && t[0] != '\0', "copy reached the clipboard: %.30s", t ? t : "(none)");
		free(t);
		flo_clipboard_set_text("pasted text");
		t = flo_clipboard_text();
		CHECK(t != NULL && strcmp(t, "pasted text") == 0, "clipboard set and read back: %s", t ? t : "(none)");
		free(t);
	}
	/* download: a file WebKit cannot show is saved under the downloads setting */
	{
		char dir[1100], file[1200], blob[1100];
		struct stat st;
		double end;
		snprintf(dir, sizeof dir, "%s/dl", argv[2]);
		snprintf(file, sizeof file, "%s/blob.bin", dir);
		snprintf(blob, sizeof blob, "file://%.*s/blob.bin", (int)(strrchr(argv[1], '/') - argv[1]), argv[1]);
		flo_pref_set("downloads", dir);
		flo_engine_set_download_handler(download_event, NULL);
		flo_page_load(page, blob);
		end = now() + 20 * scale;
		while (now() < end && download_state != FLO_DOWNLOAD_FINISHED && download_state != FLO_DOWNLOAD_FAILED) pump(50);
		CHECK(download_state == FLO_DOWNLOAD_FINISHED && stat(file, &st) == 0 && st.st_size > 0, "download saved %s (state %d)", file, download_state);
	}

	flo_page_free(page);
	flo_engine_fini();
	printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
	return failures != 0;
}
