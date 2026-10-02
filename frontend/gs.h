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
void flo_open_url(const char *url);             /* new window; NULL: the home page */
const struct plotter_table *flo_plotters(void); /* cairo plotters; ctx->priv is a cairo_t* */
const char *flo_resource_dir(void);
bool flo_js_available(void);                    /* built with FLO_JS=1 */
bool flo_js_enabled(void);
void flo_js_set(bool on);                       /* and remembered in ~/.netsurf/Choices */
void flo_download_forget(struct gui_window *gw);   /* a tab is closing: stop reporting to it */
void flo_trace(const char *stage);             /* stderr "florence: <stage>" when FLORENCE_TRACE is set */
struct plot_font_style;
void *flo_scaled_font(const struct plot_font_style *fs);   /* cairo_scaled_font_t*, cached */
struct bitmap;
void *flo_bitmap_surface(struct bitmap *bm);    /* the cairo_surface_t* behind a NetSurf bitmap */
void flo_bitmap_init(void);                     /* tells the core the pixel format (ARGB, premultiplied) */

/* ---- window operations, called by the UI (gs_window.c) -------------------
 * The Objective-C side never includes NetSurf headers: keys, mouse kinds and
 * pointers cross the boundary as the plain enums below. */
enum flo_mouse_kind {
	FLO_MOUSE_MOVE,         /* hover, no button */
	FLO_MOUSE_PRESS,        /* button 1 down */
	FLO_MOUSE_CLICK,        /* button 1 up without a drag */
	FLO_MOUSE_DOUBLE_CLICK,
	FLO_MOUSE_DRAG_START,   /* moved far enough with the button down; x,y = the press point */
	FLO_MOUSE_DRAG,         /* moving with the button down after DRAG_START */
	FLO_MOUSE_DRAG_END      /* button up after a drag */
};
enum { FLO_MOD_SHIFT = 1, FLO_MOD_CTRL = 2 };

/* keys: a Unicode code point, or one of these (all above U+10FFFF) */
enum flo_key {
	FLO_KEY_LEFT = 0x110001, FLO_KEY_RIGHT, FLO_KEY_UP, FLO_KEY_DOWN,
	FLO_KEY_PAGE_UP, FLO_KEY_PAGE_DOWN, FLO_KEY_HOME, FLO_KEY_END,
	FLO_KEY_BACKSPACE, FLO_KEY_DELETE, FLO_KEY_ESCAPE,
	FLO_KEY_SELECT_ALL, FLO_KEY_COPY, FLO_KEY_CUT, FLO_KEY_PASTE
};

enum flo_pointer { FLO_PTR_ARROW, FLO_PTR_HAND, FLO_PTR_IBEAM, FLO_PTR_CROSS, FLO_PTR_MOVE,
		   FLO_PTR_WAIT, FLO_PTR_NO };

void flo_win_navigate(struct gui_window *gw, const char *url);
void flo_win_new_tab(struct gui_window *gw, const char *url);   /* url NULL: a blank tab */
void flo_win_reload(struct gui_window *gw);
void flo_win_stop(struct gui_window *gw);
void flo_win_back(struct gui_window *gw);
void flo_win_forward(struct gui_window *gw);
bool flo_win_can_back(struct gui_window *gw);
bool flo_win_can_forward(struct gui_window *gw);
void flo_win_close(struct gui_window *gw);      /* the user closed the window */
/* paint [x0,x1) x [y0,y1) of the page, in page coordinates, onto cr (a cairo_t*) */
void flo_win_redraw(struct gui_window *gw, void *cr, int x0, int y0, int x1, int y1);
void flo_win_resize(struct gui_window *gw, int w, int h);        /* viewport size */
void flo_win_extent(struct gui_window *gw, int *w, int *h);      /* page size */
void flo_win_mouse(struct gui_window *gw, int kind, int mods, int x, int y);
bool flo_win_key(struct gui_window *gw, unsigned key, int mods); /* true: the page used it */

/* ---- UI, called by the C glue (FloUI.m) --------------------------------- */
char *flo_ui_clipboard_get(size_t *len);                         /* malloc'd UTF-8, or NULL */
void  flo_ui_clipboard_set(const char *text, size_t len);
/* flags for flo_ui_window_new */
enum { FLO_NEW_TAB = 1, FLO_NEW_FOREGROUND = 2, FLO_NEW_FOCUS_LOCATION = 4 };
/* existing_ui: the UI of the gui_window this one was created from (a tab goes in its window), or NULL */
void *flo_ui_window_new(struct gui_window *gw, void *existing_ui, int flo_new_flags);
void  flo_ui_window_free(void *ui);
void  flo_ui_invalidate(void *ui, int x0, int y0, int x1, int y1); /* x1 < 0: everything */
void  flo_ui_get_scroll(void *ui, int *x, int *y);
void  flo_ui_set_scroll(void *ui, int x, int y);
void  flo_ui_get_viewport(void *ui, int *w, int *h);
void  flo_ui_update_extent(void *ui);
void  flo_ui_set_title(void *ui, const char *title);
void  flo_ui_set_url(void *ui, const char *url);
void  flo_ui_set_status(void *ui, const char *text);
void  flo_ui_set_pointer(void *ui, int flo_pointer);   /* enum flo_pointer */
void  flo_ui_throbber(void *ui, bool on);
void  flo_ui_place_caret(void *ui, int x, int y, int height);
void  flo_ui_remove_caret(void *ui);
void  flo_ui_wake(void);                        /* a callback was scheduled: re-arm the timer */
void  flo_ui_quit(void);

#ifdef __cplusplus
}
#endif
#endif
