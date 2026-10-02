/*
 * Florence: NetSurf's window and clipboard tables, and the C half of the UI
 * bridge (the flo_win_* functions of gs.h). The UI hands over plain enums and
 * page coordinates; everything NetSurf-typed stays in this file.
 * GPL-2.0-only (see gs.h).
 */
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

#include "gnustep/gs.h"

/* ---- gui_window_table ---------------------------------------------------- */

static struct gui_window *gw_create(struct browser_window *bw, struct gui_window *existing,
				    gui_window_create_flags flags)
{
	struct gui_window *gw = calloc(1, sizeof(*gw));
	if (gw == NULL)
		return NULL;
	gw->bw = bw;
	gw->ui = flo_ui_window_new(gw);
	if (gw->ui == NULL) {
		free(gw);
		return NULL;
	}
	return gw;
}

static void gw_destroy(struct gui_window *gw)
{
	flo_ui_window_free(gw->ui);
	free(gw);
}

static nserror gw_invalidate(struct gui_window *gw, const struct rect *r)
{
	if (r == NULL)
		flo_ui_invalidate(gw->ui, 0, 0, -1, -1);
	else
		flo_ui_invalidate(gw->ui, r->x0, r->y0, r->x1, r->y1);
	return NSERROR_OK;
}

static bool gw_get_scroll(struct gui_window *gw, int *sx, int *sy)
{
	flo_ui_get_scroll(gw->ui, sx, sy);
	return true;
}

static nserror gw_set_scroll(struct gui_window *gw, const struct rect *r)
{
	flo_ui_set_scroll(gw->ui, r->x0, r->y0);
	return NSERROR_OK;
}

static nserror gw_get_dimensions(struct gui_window *gw, int *w, int *h)
{
	flo_ui_get_viewport(gw->ui, w, h);
	return NSERROR_OK;
}

static nserror gw_event(struct gui_window *gw, enum gui_window_event ev)
{
	switch (ev) {
	case GW_EVENT_UPDATE_EXTENT: flo_ui_update_extent(gw->ui); break;
	case GW_EVENT_REMOVE_CARET: flo_ui_remove_caret(gw->ui); break;
	case GW_EVENT_START_THROBBER: flo_ui_throbber(gw->ui, true); break;
	case GW_EVENT_STOP_THROBBER: flo_ui_throbber(gw->ui, false); break;
	default: break;
	}
	return NSERROR_OK;
}

static void gw_set_title(struct gui_window *gw, const char *title) { flo_ui_set_title(gw->ui, title); }

static nserror gw_set_url(struct gui_window *gw, struct nsurl *url)
{
	flo_ui_set_url(gw->ui, nsurl_access(url));
	return NSERROR_OK;
}

static void gw_set_status(struct gui_window *gw, const char *text) { flo_ui_set_status(gw->ui, text); }

static void gw_set_pointer(struct gui_window *gw, enum gui_pointer_shape shape)
{
	int p;
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

static nserror gw_place_caret(struct gui_window *gw, int x, int y, int height, const struct rect *clip)
{
	flo_ui_place_caret(gw->ui, x, y, height);
	return NSERROR_OK;
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

void flo_win_reload(struct gui_window *gw) { browser_window_reload(gw->bw, true); }
void flo_win_stop(struct gui_window *gw) { browser_window_stop(gw->bw); }
void flo_win_back(struct gui_window *gw) { browser_window_history_back(gw->bw, false); }
void flo_win_forward(struct gui_window *gw) { browser_window_history_forward(gw->bw, false); }
bool flo_win_can_back(struct gui_window *gw) { return browser_window_back_available(gw->bw); }
bool flo_win_can_forward(struct gui_window *gw) { return browser_window_forward_available(gw->bw); }
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
