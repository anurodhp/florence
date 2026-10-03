/*
 * Florence: NetSurf's window and clipboard tables, and the C half of the UI
 * bridge (the flo_win_* functions of gs.h). The UI hands over plain enums and
 * page coordinates; everything NetSurf-typed stays in this file.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/errors.h"
#include "utils/nsurl.h"
#include "netsurf/types.h"
#include "netsurf/browser_window.h"
#include "netsurf/mouse.h"
#include "netsurf/keypress.h"
#include "netsurf/plotters.h"
#include "netsurf/window.h"
#include "netsurf/clipboard.h"
#include "netsurf/content.h"
#include "netsurf/content_type.h"
#include "desktop/search.h"
#include "content/urldb.h"

#include "desktop/browser_history.h"         /* browser_window_history_back/forward/..._available */

#include "gnustep/gs.h"

/* ---- gui_window_table ---------------------------------------------------- */

/* the core can call back while gw_create is still running; every callback tolerates no UI yet */
#define NOUI(gw) ((gw)->ui == NULL)

static struct gui_window *gw_create(struct browser_window *bw, struct gui_window *existing,
				    gui_window_create_flags flags)
{
	struct gui_window *gw = calloc(1, sizeof(*gw));
	if (gw == NULL)
		return NULL;
	gw->bw = bw;
	flo_trace("window: create");
	{
		void *existing_ui = existing != NULL ? existing->ui : NULL;
		int fl = 0;

		if ((flags & GW_CREATE_TAB) && existing_ui != NULL)
			fl |= FLO_NEW_TAB;
		if (flags & GW_CREATE_FOREGROUND)
			fl |= FLO_NEW_FOREGROUND;
		if (flags & GW_CREATE_FOCUS_LOCATION)
			fl |= FLO_NEW_FOCUS_LOCATION;
		gw->ui = flo_ui_window_new(gw, existing_ui, fl);   /* the UI also stores itself in gw->ui at once */
	}
	if (gw->ui == NULL) {
		free(gw);
		return NULL;
	}
	return gw;
}

static void gw_destroy(struct gui_window *gw)
{
	flo_download_forget(gw);
	if (gw->ui != NULL)
		flo_ui_window_free(gw->ui);
	free(gw);
	flo_memory_note();
}

static nserror gw_invalidate(struct gui_window *gw, const struct rect *r)
{
	flo_trace("window: invalidate");
	if (NOUI(gw))
		return NSERROR_OK;
	if (r == NULL)
		flo_ui_invalidate(gw->ui, 0, 0, -1, -1);
	else
		flo_ui_invalidate(gw->ui, r->x0, r->y0, r->x1, r->y1);
	return NSERROR_OK;
}

static bool gw_get_scroll(struct gui_window *gw, int *sx, int *sy)
{
	if (NOUI(gw)) {
		*sx = *sy = 0;
		return true;
	}
	flo_ui_get_scroll(gw->ui, sx, sy);
	return true;
}

static nserror gw_set_scroll(struct gui_window *gw, const struct rect *r)
{
	if (NOUI(gw))
		return NSERROR_OK;
	flo_ui_set_scroll(gw->ui, r->x0, r->y0);
	return NSERROR_OK;
}

static nserror gw_get_dimensions(struct gui_window *gw, int *w, int *h)
{
	if (NOUI(gw)) {
		*w = 960;
		*h = 600;
		return NSERROR_OK;
	}
	flo_ui_get_viewport(gw->ui, w, h);
	return NSERROR_OK;
}

static nserror gw_event(struct gui_window *gw, enum gui_window_event ev)
{
	if (NOUI(gw))
		return NSERROR_OK;
	switch (ev) {
	case GW_EVENT_UPDATE_EXTENT: flo_ui_update_extent(gw->ui); break;
	case GW_EVENT_REMOVE_CARET: flo_ui_remove_caret(gw->ui); break;
	case GW_EVENT_START_THROBBER: flo_ui_throbber(gw->ui, true); break;
	case GW_EVENT_STOP_THROBBER: flo_ui_throbber(gw->ui, false); flo_memory_note(); break;
	default: break;
	}
	return NSERROR_OK;
}

static void gw_set_title(struct gui_window *gw, const char *title) { if (!NOUI(gw)) flo_ui_set_title(gw->ui, title); }

static nserror gw_set_url(struct gui_window *gw, struct nsurl *url)
{
	if (NOUI(gw))
		return NSERROR_OK;
	flo_ui_set_url(gw->ui, nsurl_access(url));
	return NSERROR_OK;
}

static void gw_set_status(struct gui_window *gw, const char *text) { if (!NOUI(gw)) flo_ui_set_status(gw->ui, text); }

static void gw_set_pointer(struct gui_window *gw, enum gui_pointer_shape shape)
{
	int p;
	if (NOUI(gw))
		return;
	switch (shape) {
	case GUI_POINTER_POINT: p = FLO_PTR_HAND; break;
	case GUI_POINTER_CARET: p = FLO_PTR_IBEAM; break;
	case GUI_POINTER_CROSS: p = FLO_PTR_CROSS; break;
	case GUI_POINTER_MOVE: p = FLO_PTR_MOVE; break;
	case GUI_POINTER_WAIT:
	case GUI_POINTER_PROGRESS: p = FLO_PTR_WAIT; break;
	case GUI_POINTER_NO_DROP:
	case GUI_POINTER_NOT_ALLOWED: p = FLO_PTR_NO; break;
	default: p = FLO_PTR_ARROW; break;
	}
	flo_ui_set_pointer(gw->ui, p);
}

static void gw_place_caret(struct gui_window *gw, int x, int y, int height, const struct rect *clip)
{
	if (!NOUI(gw))
		flo_ui_place_caret(gw->ui, x, y, height);
}

static void gw_set_icon(struct gui_window *gw, struct hlcache_handle *icon)
{
	if (icon != NULL)
		flo_favicon_save(gw->bw, icon);       /* kept for the start page's tiles */
}

static struct gui_window_table window_table = {
	.create = gw_create,
	.destroy = gw_destroy,
	.invalidate = gw_invalidate,
	.get_scroll = gw_get_scroll,
	.set_scroll = gw_set_scroll,
	.get_dimensions = gw_get_dimensions,
	.event = gw_event,
	.set_title = gw_set_title,
	.set_url = gw_set_url,
	.set_status = gw_set_status,
	.set_icon = gw_set_icon,
	.set_pointer = gw_set_pointer,
	.place_caret = gw_place_caret,
};

struct gui_window_table *flo_window_table = &window_table;

/* ---- gui_clipboard_table ------------------------------------------------- */

static void clip_get(char **buffer, size_t *length)
{
	*buffer = flo_ui_clipboard_get(length);
	if (*buffer == NULL)
		*length = 0;
}

static void clip_set(const char *buffer, size_t length, nsclipboard_styles styles[], int n_styles)
{
	flo_ui_clipboard_set(buffer, length);
}

static struct gui_clipboard_table clipboard_table = { .get = clip_get, .set = clip_set };
struct gui_clipboard_table *flo_clipboard_table = &clipboard_table;

/* ---- the UI's calls into the core ---------------------------------------- */

void flo_win_navigate(struct gui_window *gw, const char *url)
{
	nsurl *u = NULL;
	if (nsurl_create(url, &u) != NSERROR_OK)
		return;
	browser_window_navigate(gw->bw, u, NULL, BW_NAVIGATE_HISTORY, NULL, NULL, NULL);
	nsurl_unref(u);
}

void flo_win_new_tab(struct gui_window *gw, const char *url)
{
	nsurl *u = NULL;
	struct browser_window *bw = NULL;

	if (url == NULL)
		url = flo_startpage_url();        /* a new tab shows the start page (NULL if it cannot be written: blank) */
	if (url != NULL && nsurl_create(url, &u) != NSERROR_OK)
		return;
	browser_window_create(BW_CREATE_HISTORY | BW_CREATE_TAB | BW_CREATE_FOREGROUND | BW_CREATE_FOCUS_LOCATION,
			      u, NULL, gw->bw, &bw);
	if (u != NULL)
		nsurl_unref(u);
}

void flo_win_features(struct gui_window *gw, int x, int y, struct flo_features *f)
{
	struct browser_window_features bf;
	char *sel;

	memset(f, 0, sizeof(*f));
	memset(&bf, 0, sizeof(bf));
	if (browser_window_get_features(gw->bw, x, y, &bf) == NSERROR_OK) {
		/* both URLs belong to the page: copy them, release nothing */
		if (bf.link != NULL)
			f->link = strdup(nsurl_access(bf.link));
		if (bf.object != NULL && content_get_type(bf.object) == CONTENT_IMAGE) {
			nsurl *iu = hlcache_handle_get_url(bf.object);

			if (iu != NULL)
				f->image = strdup(nsurl_access(iu));
		}
		f->text_field = bf.form_features == CTX_FORM_TEXT;
	}
	sel = browser_window_get_selection(gw->bw);
	if (sel != NULL) {
		f->selection = sel[0] != '\0';
		free(sel);
	}
}

void flo_win_open_link_tab(struct gui_window *gw, const char *url, bool foreground)
{
	nsurl *u = NULL;
	struct browser_window *bw = NULL;

	if (url == NULL || nsurl_create(url, &u) != NSERROR_OK)
		return;
	browser_window_create(BW_CREATE_HISTORY | BW_CREATE_TAB | (foreground ? BW_CREATE_FOREGROUND : 0),
			      u, browser_window_access_url(gw->bw), gw->bw, &bw);
	nsurl_unref(u);
}

void flo_win_find(struct gui_window *gw, const char *text, bool forwards, bool case_sensitive)
{
	if (text == NULL || text[0] == '\0') {
		browser_window_search_clear(gw->bw);
		return;
	}
	browser_window_search(gw->bw, gw->ui,
			      (forwards ? SEARCH_FLAG_FORWARDS : SEARCH_FLAG_BACKWARDS) |
			      (case_sensitive ? SEARCH_FLAG_CASE_SENSITIVE : 0) | SEARCH_FLAG_SHOWALL,
			      text);
}

void flo_win_find_clear(struct gui_window *gw) { browser_window_search_clear(gw->bw); }

int flo_win_zoom(struct gui_window *gw, int step)
{
	float now = browser_window_get_scale(gw->bw), next;
	char msg[32];

	if (step == 0)
		next = 1.0f;
	else
		next = now + 0.1f * (float)step;
	if (next < 0.3f || next > 3.0f)
		return (int)(now * 100 + 0.5f);
	browser_window_set_scale(gw->bw, next, true);
	snprintf(msg, sizeof(msg), "Zoom %d%%", (int)(next * 100 + 0.5f));
	flo_ui_set_status(gw->ui, msg);
	return (int)(next * 100 + 0.5f);
}

void flo_win_reload(struct gui_window *gw) { browser_window_reload(gw->bw, true); }
void flo_win_stop(struct gui_window *gw) { browser_window_stop(gw->bw); }
void flo_win_back(struct gui_window *gw) { browser_window_history_back(gw->bw, false); }
void flo_win_forward(struct gui_window *gw) { browser_window_history_forward(gw->bw, false); }
int flo_win_security(struct gui_window *gw)
{
	struct nsurl *u = browser_window_access_url(gw->bw);

	if (u == NULL || strncmp(nsurl_access(u), "https:", 6) != 0)
		return 0;
	/* libcurl verifies the peer unless this permission was granted (content/fetchers/curl.c) */
	return urldb_get_cert_permissions(u) ? 2 : 1;
}

bool flo_win_can_back(struct gui_window *gw) { return browser_window_history_back_available(gw->bw); }
bool flo_win_can_forward(struct gui_window *gw) { return browser_window_history_forward_available(gw->bw); }
void flo_win_close(struct gui_window *gw) { browser_window_destroy(gw->bw); }

void flo_win_redraw(struct gui_window *gw, void *cr, int x0, int y0, int x1, int y1)
{
	struct rect clip = { x0, y0, x1, y1 };
	struct redraw_context ctx = {
		.interactive = true,
		.background_images = true,
		.plot = flo_plotters(),
		.priv = cr,
	};
	browser_window_redraw(gw->bw, 0, 0, &clip, &ctx);
}

void flo_win_resize(struct gui_window *gw, int w, int h) { browser_window_set_dimensions(gw->bw, w, h); }

void flo_win_extent(struct gui_window *gw, int *w, int *h)
{
	*w = *h = 0;
	browser_window_get_extents(gw->bw, false, w, h);
}

static browser_mouse_state mods_to_state(int mods)
{
	return (mods & FLO_MOD_SHIFT ? BROWSER_MOUSE_MOD_1 : 0) | (mods & FLO_MOD_CTRL ? BROWSER_MOUSE_MOD_2 : 0);
}

void flo_win_mouse(struct gui_window *gw, int kind, int mods, int x, int y)
{
	browser_mouse_state m = mods_to_state(mods);
	switch (kind) {
	case FLO_MOUSE_MOVE: browser_window_mouse_track(gw->bw, m, x, y); break;
	case FLO_MOUSE_PRESS: browser_window_mouse_click(gw->bw, BROWSER_MOUSE_PRESS_1 | m, x, y); break;
	case FLO_MOUSE_CLICK: browser_window_mouse_click(gw->bw, BROWSER_MOUSE_CLICK_1 | m, x, y); break;
	case FLO_MOUSE_DOUBLE_CLICK:
		browser_window_mouse_click(gw->bw, BROWSER_MOUSE_CLICK_1 | BROWSER_MOUSE_DOUBLE_CLICK | m, x, y);
		break;
	case FLO_MOUSE_DRAG_START: browser_window_mouse_click(gw->bw, BROWSER_MOUSE_DRAG_1 | m, x, y); break;
	case FLO_MOUSE_DRAG:
		browser_window_mouse_track(gw->bw, BROWSER_MOUSE_HOLDING_1 | BROWSER_MOUSE_DRAG_ON | m, x, y);
		break;
	case FLO_MOUSE_DRAG_END: browser_window_mouse_track(gw->bw, 0, x, y); break;
	}
}

bool flo_win_key(struct gui_window *gw, unsigned key, int mods)
{
	uint32_t k;
	switch (key) {
	case FLO_KEY_LEFT: k = NS_KEY_LEFT; break;
	case FLO_KEY_RIGHT: k = NS_KEY_RIGHT; break;
	case FLO_KEY_UP: k = NS_KEY_UP; break;
	case FLO_KEY_DOWN: k = NS_KEY_DOWN; break;
	case FLO_KEY_PAGE_UP: k = NS_KEY_PAGE_UP; break;
	case FLO_KEY_PAGE_DOWN: k = NS_KEY_PAGE_DOWN; break;
	case FLO_KEY_HOME: k = (mods & FLO_MOD_CTRL) ? NS_KEY_TEXT_START : NS_KEY_LINE_START; break;
	case FLO_KEY_END: k = (mods & FLO_MOD_CTRL) ? NS_KEY_TEXT_END : NS_KEY_LINE_END; break;
	case FLO_KEY_BACKSPACE: k = NS_KEY_DELETE_LEFT; break;
	case FLO_KEY_DELETE: k = NS_KEY_DELETE_RIGHT; break;
	case FLO_KEY_ESCAPE: k = NS_KEY_ESCAPE; break;
	case FLO_KEY_SELECT_ALL: k = NS_KEY_SELECT_ALL; break;
	case FLO_KEY_COPY: k = NS_KEY_COPY_SELECTION; break;
	case FLO_KEY_CUT: k = NS_KEY_CUT_SELECTION; break;
	case FLO_KEY_PASTE: k = NS_KEY_PASTE; break;
	case '\r': k = NS_KEY_NL; break;
	default: k = key; break;
	}
	return browser_window_key_press(gw->bw, k);
}
