/*
 * Florence: the GNUstep frontend of NetSurf. Shared declarations between the C
 * glue (the NetSurf callback tables) and the Objective-C user interface.
 * GPL-2.0-only, like NetSurf: this links into it and its Makefile fragments and
 * the schedule/filetype/fetch files are derived from the monkey frontend.
 */
#ifndef FLORENCE_GS_H
#define FLORENCE_GS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct browser_window;
struct gui_window;
struct redraw_context;
struct plotter_table;

/* the C side's per-window record; the UI keeps `ui` (an Objective-C object). */
struct gui_window {
	struct browser_window *bw;
	void *ui;
};

/* ---- C glue, called by the UI ------------------------------------------- */
int  flo_core_init(int argc, char **argv);      /* options, tables, netsurf_init */
void flo_core_fini(void);
int  flo_schedule_run(void);                    /* ms until the next callback, -1 none */
int  flo_schedule_next(void);                   /* same, without running anything */
void flo_open_url(const char *url);             /* new window; NULL: the home page */
const struct plotter_table *flo_plotters(void); /* cairo plotters; ctx->priv is a cairo_t* */
const char *flo_resource_dir(void);
struct plot_font_style;
void *flo_scaled_font(const struct plot_font_style *fs);   /* cairo_scaled_font_t*, cached */
struct bitmap;
void *flo_bitmap_surface(struct bitmap *bm);    /* the cairo_surface_t* behind a NetSurf bitmap */
void flo_bitmap_init(void);                     /* tells the core the pixel format (ARGB, premultiplied) */

/* ---- UI, called by the C glue (FloUI.m) --------------------------------- */
void *flo_ui_window_new(struct gui_window *gw);
void  flo_ui_window_free(void *ui);
void  flo_ui_invalidate(void *ui, int x0, int y0, int x1, int y1); /* x1 < 0: everything */
void  flo_ui_get_scroll(void *ui, int *x, int *y);
void  flo_ui_set_scroll(void *ui, int x, int y);
void  flo_ui_get_viewport(void *ui, int *w, int *h);
void  flo_ui_update_extent(void *ui);
void  flo_ui_set_title(void *ui, const char *title);
void  flo_ui_set_url(void *ui, const char *url);
void  flo_ui_set_status(void *ui, const char *text);
void  flo_ui_set_pointer(void *ui, int gui_pointer_shape);
void  flo_ui_throbber(void *ui, bool on);
void  flo_ui_place_caret(void *ui, int x, int y, int height);
void  flo_ui_remove_caret(void *ui);
void  flo_ui_wake(void);                        /* a callback was scheduled: re-arm the timer */
void  flo_ui_quit(void);

#ifdef __cplusplus
}
#endif
#endif
