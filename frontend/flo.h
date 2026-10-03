/*
 * Florence: the line between the GNUstep user interface (Objective-C, Flo*.m) and the engine
 * (plain C, flo_*.c, which alone includes WebKit and GLib headers). Nothing else crosses it.
 *
 * The engine renders on the CPU into shared-memory buffers (WPE WebKit, Skia raster); the UI
 * asks for the pixels of a rectangle and draws them. There is no GPU path and no toolkit in
 * the engine: input comes in as plain numbers, pixels go out as BGRA.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef FLORENCE_FLO_H
#define FLORENCE_FLO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the GLib main loop, driven by the UI's run loop -------------------------------------------
 * WebKit's UI-process half lives on GLib's default main context. The UI does not poll it: it asks
 * what to wait for (file descriptors and a timeout), sleeps in its own run loop, and calls
 * flo_glib_dispatch() when one of them fires. See FloGLib.m. */
#define FLO_GLIB_MAX_FDS 64
struct flo_glib_wait {
	int  n;                         /* descriptors to watch */
	int  fd[FLO_GLIB_MAX_FDS];
	bool read[FLO_GLIB_MAX_FDS];    /* watch for readable */
	bool write[FLO_GLIB_MAX_FDS];   /* watch for writable */
	int  timeout_ms;                /* -1 none, 0 something is ready now */
};
bool flo_glib_prepare(struct flo_glib_wait *w);   /* what to wait for; false: nothing can ever wake us */
void flo_glib_dispatch(void);                     /* a descriptor or the timeout fired: run what is ready */
/* Other threads wake the context with g_main_context_wakeup(), which is a descriptor in the list
 * above: no separate waker is needed. Call prepare, wait, dispatch, prepare... never two prepares. */

/* ---- engine ----------------------------------------------------------------------------------- */
int  flo_engine_init(const char *data_dir, const char *cache_dir);   /* 0 ok; sets the low-power environment first */
void flo_engine_fini(void);
bool flo_engine_javascript(void);               /* off until the user opts in */
void flo_engine_set_javascript(bool on);        /* every page, now and later */

/* ---- one page (a web view) ---------------------------------------------------------------------- */
struct flo_page;

struct flo_page_events {
	void (*changed)(void *ui, int x, int y, int w, int h);   /* pixels changed (page coordinates) */
	void (*title)(void *ui, const char *title);
	void (*uri)(void *ui, const char *uri);
	void (*progress)(void *ui, double fraction);             /* 0..1; 1 when done */
	void (*nav_state)(void *ui, bool can_back, bool can_forward);
	void (*hover)(void *ui, const char *link);               /* link under the pointer, or NULL */
};

struct flo_page *flo_page_new(const struct flo_page_events *ev, void *ui, int w, int h);
void flo_page_free(struct flo_page *p);
void flo_page_load(struct flo_page *p, const char *uri);
void flo_page_reload(struct flo_page *p);
void flo_page_stop(struct flo_page *p);
void flo_page_back(struct flo_page *p);
void flo_page_forward(struct flo_page *p);
void flo_page_resize(struct flo_page *p, int w, int h);  /* the viewport, in pixels */
bool flo_page_loading(struct flo_page *p);

/* The newest finished frame: BGRA, premultiplied, rows top-down. Valid until the next call into
 * the engine; NULL before the first frame. */
const uint8_t *flo_page_pixels(struct flo_page *p, int *w, int *h, int *stride);

/* ---- input (coordinates in page pixels, origin top left) -------------------------------------- */
enum { FLO_MOD_SHIFT = 1, FLO_MOD_CTRL = 2, FLO_MOD_ALT = 4, FLO_MOD_META = 8 };
void flo_page_pointer_move(struct flo_page *p, int mods, double x, double y);
void flo_page_pointer_button(struct flo_page *p, int mods, int button, bool down, int clicks, double x, double y);
void flo_page_scroll(struct flo_page *p, int mods, double dx, double dy, double x, double y);   /* wheel notches, + is down/right */
void flo_page_key(struct flo_page *p, int mods, uint32_t codepoint, bool down);   /* printable key */
enum flo_special_key {
	FLO_KEY_ENTER = 1, FLO_KEY_TAB, FLO_KEY_BACKSPACE, FLO_KEY_DELETE, FLO_KEY_ESCAPE,
	FLO_KEY_LEFT, FLO_KEY_RIGHT, FLO_KEY_UP, FLO_KEY_DOWN,
	FLO_KEY_HOME, FLO_KEY_END, FLO_KEY_PAGE_UP, FLO_KEY_PAGE_DOWN
};
void flo_page_special_key(struct flo_page *p, int mods, int key, bool down);
void flo_page_focus(struct flo_page *p, bool focused);

#ifdef __cplusplus
}
#endif
#endif
