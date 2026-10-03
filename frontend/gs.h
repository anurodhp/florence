/*
 * Florence: the GNUstep frontend of NetSurf. Shared declarations between the C glue (the NetSurf
 * callback tables) and the Objective-C user interface: the only place they meet.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only
 * Florence is GPL-2.0-only, like the NetSurf core it links and some files here derive from; see LICENSE.
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
void flo_install_xio_handler(void);             /* report who was running when the X connection failed */
void flo_memory_note(void);                     /* memory just changed hands: check it soon, and drop unused cache if low */
int  gs_memory_level(void);                     /* 0 fine (or unknown), 1 low */
int  gs_memory_level_for(uint64_t avail_bytes, uint64_t total_bytes);
void gs_memory_purge(void);
int  flo_schedule_run(void);                    /* ms until the next callback, -1 none */
void flo_open_url(const char *url);             /* new window; NULL: the home page */
const struct plotter_table *flo_plotters(void); /* cairo plotters; ctx->priv is a cairo_t* */
const char *flo_resource_dir(void);
const char *flo_startpage_url(void);            /* writes the start page; its file: URL */
struct browser_window;
struct hlcache_handle;
void flo_favicon_save(struct browser_window *bw, struct hlcache_handle *icon);   /* the core's favicon for bw's page */
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
/* what the address bar's padlock may claim: 0 nothing (not https), 1 https and the certificate check ran,
 * 2 https but the user chose to continue past a failed check for this site */
int  flo_win_security(struct gui_window *gw);
bool flo_win_can_back(struct gui_window *gw);
bool flo_win_can_forward(struct gui_window *gw);
void flo_win_close(struct gui_window *gw);      /* the user closed the window */
/* paint [x0,x1) x [y0,y1) of the page, in page coordinates, onto cr (a cairo_t*) */
void flo_win_redraw(struct gui_window *gw, void *cr, int x0, int y0, int x1, int y1);
void flo_win_resize(struct gui_window *gw, int w, int h);        /* viewport size */
void flo_win_extent(struct gui_window *gw, int *w, int *h);      /* page size */
void flo_win_mouse(struct gui_window *gw, int kind, int mods, int x, int y);
bool flo_win_key(struct gui_window *gw, unsigned key, int mods); /* true: the page used it */

/* What is under a point of the page (for the context menu and Cmd-click). The strings are malloc'd:
 * free() them. */
struct flo_features {
	char *link;                     /* the link's address, or NULL */
	char *image;                    /* the image's address, or NULL */
	bool text_field;                /* a text input */
	bool selection;                 /* the page has selected text */
};
void flo_win_features(struct gui_window *gw, int x, int y, struct flo_features *f);
void flo_win_open_link_tab(struct gui_window *gw, const char *url, bool foreground);

/* find in page; results come back through flo_ui_find_status */
void flo_win_find(struct gui_window *gw, const char *text, bool forwards, bool case_sensitive);
void flo_win_find_clear(struct gui_window *gw);

/* zoom: step +1/-1 (10 %), 0 resets to 100 %; returns the new percentage */
int  flo_win_zoom(struct gui_window *gw, int step);

/* the content blocker: Safari's content-blocker JSON lists (gs_blocker.c) */
bool flo_fetch_blocked(const char *url, const char *referrer);  /* NetSurf's fetch_start() asks this */
int  flo_blocker_setup(const char *default_list, const char *dir, const char *css_path, const char *cache_path);   /* rules, from the cache if it is current */
void flo_blocker_reload(void);                  /* rescan the lists (gs_core.c) */
void flo_blocker_clear(void);
int  flo_blocker_load_file(const char *path);   /* rules added, or -1 */
int  flo_blocker_load_dir(const char *dir);     /* files loaded */
void flo_blocker_finish(const char *css_path);  /* build the index; write the cosmetic stylesheet there */
bool flo_blocker_enabled(void);
void flo_blocker_enable(bool on);
int  flo_blocker_rule_count(void);
int  flo_blocker_file_count(void);
int  flo_blocker_css_count(void);
unsigned long flo_blocker_blocked_count(void);

/* EasyList (gs_lists.c): a weekly background download, converted to a content-blocker list */
#include <time.h>
long flo_abp_convert(const char *abp_in, const char *json_out);          /* rules written, or -1 */
void gs_lists_start(const char *home, const char *ca_bundle, bool force);
void flo_lists_update(bool force);              /* now if due (or forced); no-op while one runs */
time_t flo_lists_updated(void);                 /* when the downloaded list was installed, 0 never */
bool flo_lists_busy(void);
int  flo_lists_last_result(void);               /* 0 none yet, 1 updated, -1 failed */
void flo_ui_lists_updated(void);                /* UI, any thread: the blocker should reload */

/* settings, remembered in ~/.netsurf/Choices */
bool flo_opt_hide_ads(void);
void flo_opt_set_hide_ads(bool on);
bool flo_opt_dnt(void);
void flo_opt_set_dnt(bool on);
int  flo_opt_font_min(void);                    /* tenths of a point: 85 = 8.5 pt */
void flo_opt_set_font_min(int tenths);
bool flo_opt_referer(void);                     /* send the Referer header */
void flo_opt_set_referer(bool on);
const char *flo_opt_homepage(void);             /* "" when none */
void flo_opt_set_homepage(const char *url);
void flo_clear_cookies(void);                   /* the cookie file goes now, the in-memory jar is not saved at quit */
void flo_clear_history(void);                   /* likewise the visited-URL database */

/* Florence's own settings (~/.netsurf/Florence.conf): search engine, downloads folder, cache size ... */
const char *flo_pref_get(const char *key, const char *def);
void flo_pref_set(const char *key, const char *val);

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
void  flo_ui_find_status(void *ui, bool found);   /* the last find matched / did not */
void  flo_ui_wake(void);                        /* a callback was scheduled: re-arm the timer */
void  flo_ui_quit(void);

#ifdef __cplusplus
}
#endif
#endif
