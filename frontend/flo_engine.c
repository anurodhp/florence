/*
 * Florence: the engine glue. WPE WebKit (software rendering, no GPU) behind the plain C interface in
 * flo.h. This file and flo_platform.c are the only ones that include WebKit or GLib headers.
 *
 * Small-machine rules, as in the NetSurf frontend this replaces: nothing runs while the page is
 * idle (GLib's main context is driven from the UI's run loop and sleeps in it, see flo_glib_*),
 * frames are capped at 30 per second and arrive as damage rectangles, caches are the smallest
 * WebKit offers.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#include "flo.h"
#include "flo_platform.h"

#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <wpe/webkit.h>

/* ---- GLib's main context, pumped from outside ---------------------------------------------------- */

static GMainContext *glib_ctx;
static GPollFD glib_fds[FLO_GLIB_MAX_FDS];
static gint glib_nfds, glib_prio;
static gboolean glib_prepared;

bool flo_glib_prepare(struct flo_glib_wait *w)
{
	gint timeout = -1, n, i;

	if (glib_ctx == NULL) {
		glib_ctx = g_main_context_default();
		if (!g_main_context_acquire(glib_ctx))
			return false;
	}
	/* prepare/check must come in pairs: a second prepare before dispatch is the caller's bug */
	g_return_val_if_fail(!glib_prepared, false);
	if (g_main_context_prepare(glib_ctx, &glib_prio))
		timeout = 0;
	n = g_main_context_query(glib_ctx, glib_prio, &timeout, glib_fds, FLO_GLIB_MAX_FDS);
	if (n > FLO_GLIB_MAX_FDS) {
		g_warning("florence: the main context wants %d descriptors, room for %d", n, FLO_GLIB_MAX_FDS);
		n = FLO_GLIB_MAX_FDS;           /* the rest are not watched; their sources wait for the next timeout */
	}
	glib_nfds = n;
	glib_prepared = TRUE;
	w->n = n;
	for (i = 0; i < n; i++) {
		w->fd[i] = glib_fds[i].fd;
		w->read[i] = (glib_fds[i].events & (G_IO_IN | G_IO_HUP | G_IO_ERR)) != 0;
		w->write[i] = (glib_fds[i].events & G_IO_OUT) != 0;
	}
	w->timeout_ms = timeout;
	return true;
}

void flo_glib_dispatch(void)
{
	if (glib_ctx == NULL || !glib_prepared)
		return;
	glib_prepared = FALSE;
	g_poll(glib_fds, glib_nfds, 0);         /* the UI says something fired: collect which */
	if (g_main_context_check(glib_ctx, glib_prio, glib_fds, glib_nfds))
		g_main_context_dispatch(glib_ctx);
}

/* ---- engine -------------------------------------------------------------------------------------- */

static WPEDisplay *display;
static WebKitNetworkSession *session;
static WebKitWebContext *context;
static WebKitSettings *settings;        /* shared by every page, so one switch changes all */

static void setdefault(const char *name, const char *value) { setenv(name, value, 0); }

int flo_engine_init(const char *data_dir, const char *cache_dir)
{
	GError *error = NULL;

	/* Read by WebKit's processes at start-up (they inherit our environment); setdefault keeps
	 * anything the user put there for experiments. Each is a source line in the pinned tree: */
	setdefault("WEBKIT_DISABLE_COMPOSITING_MODE", "1");      /* scripts/webkit_fixes.sh: no GL compositor, paint on the CPU */
	setdefault("WEBKIT_SKIA_ENABLE_CPU_RENDERING", "1");     /* WebProcessGLib.cpp: no GPU buffers, shared memory */
	setdefault("WEBKIT_SKIA_CPU_PAINTING_THREADS", "1");     /* SkiaPaintingEngine.cpp: default is half the cores */
	setdefault("WEBKIT_FORCE_VBLANK_TIMER", "1");            /* DisplayVBlankMonitor.cpp: no screen to ask */
	setdefault("WEBKIT_DISPLAY_REFRESH_THROTTLE_FPS", "30"); /* DisplayLinkGLib.cpp: a factor of the 60 Hz timer */

	display = flo_display_new();
	if (!wpe_display_connect(display, &error)) {
		g_printerr("florence: display: %s\n", error->message);
		g_error_free(error);
		return -1;
	}
	wpe_display_set_primary(display);

	session = webkit_network_session_new(data_dir, cache_dir);
	context = webkit_web_context_new();
	/* the smallest memory footprint WebKit has a name for: no page cache, minimal resource caches */
	webkit_web_context_set_cache_model(context, WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
	webkit_network_session_set_itp_enabled(session, FALSE);  /* tracking-prevention bookkeeping: a database and work */

	settings = webkit_settings_new();
	webkit_settings_set_enable_javascript(settings, FALSE);  /* until the user opts in */
	/* No hardware-acceleration policy here: that setting is GTK's. WPE's CPU path is the environment
	 * variable above. DNS prefetching, hyperlink auditing and the offline application cache have
	 * deprecated setters that do nothing in this WebKit, so they are not called. */
	webkit_settings_set_enable_page_cache(settings, FALSE);
	webkit_settings_set_enable_smooth_scrolling(settings, FALSE);
	webkit_settings_set_enable_html5_database(settings, FALSE);
	webkit_settings_set_enable_developer_extras(settings, FALSE);
	webkit_settings_set_enable_media(settings, FALSE);
	webkit_settings_set_enable_mediasource(settings, FALSE);
	webkit_settings_set_enable_media_stream(settings, FALSE);
	webkit_settings_set_enable_webaudio(settings, FALSE);
	webkit_settings_set_enable_webgl(settings, FALSE);
	return 0;
}

void flo_engine_fini(void)
{
	g_clear_object(&settings);
	g_clear_object(&context);
	g_clear_object(&session);
	g_clear_object(&display);
}

bool flo_engine_javascript(void)
{
	return settings != NULL && webkit_settings_get_enable_javascript(settings);
}

void flo_engine_set_javascript(bool on)
{
	if (settings != NULL)
		webkit_settings_set_enable_javascript(settings, on);
}

/* ---- pages --------------------------------------------------------------------------------------- */

struct flo_page {
	WebKitWebView *web;
	WPEView *view;
	struct flo_page_events ev;
	void *ui;
	unsigned buttons;               /* pointer buttons held, as WPE modifier bits */
};

static void on_frame(WPEView *view, int x, int y, int w, int h, gpointer data)
{
	struct flo_page *p = data;
	(void)view;
	if (p->ev.changed != NULL)
		p->ev.changed(p->ui, x, y, w, h);
}

static void report_nav(struct flo_page *p)
{
	if (p->ev.nav_state != NULL)
		p->ev.nav_state(p->ui, webkit_web_view_can_go_back(p->web), webkit_web_view_can_go_forward(p->web));
}

static void on_title(GObject *o, GParamSpec *spec, gpointer data)
{
	struct flo_page *p = data;
	(void)o; (void)spec;
	if (p->ev.title != NULL)
		p->ev.title(p->ui, webkit_web_view_get_title(p->web));
}

static void on_uri(GObject *o, GParamSpec *spec, gpointer data)
{
	struct flo_page *p = data;
	(void)o; (void)spec;
	if (p->ev.uri != NULL)
		p->ev.uri(p->ui, webkit_web_view_get_uri(p->web));
}

static void on_progress(GObject *o, GParamSpec *spec, gpointer data)
{
	struct flo_page *p = data;
	(void)o; (void)spec;
	if (p->ev.progress != NULL)
		p->ev.progress(p->ui, webkit_web_view_get_estimated_load_progress(p->web));
}

static void on_load_changed(WebKitWebView *web, WebKitLoadEvent event, gpointer data)
{
	struct flo_page *p = data;
	(void)web;
	if (event == WEBKIT_LOAD_FINISHED && p->ev.progress != NULL)
		p->ev.progress(p->ui, 1.0);
	if (event == WEBKIT_LOAD_COMMITTED || event == WEBKIT_LOAD_FINISHED)
		report_nav(p);
}

static gboolean on_load_failed(WebKitWebView *web, WebKitLoadEvent event, const char *uri, GError *error, gpointer data)
{
	char *msg, *html;
	(void)event; (void)data;
	if (g_error_matches(error, WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED))
		return FALSE;           /* the user stopped it, or navigated away */
	msg = g_markup_escape_text(error->message, -1);
	html = g_strdup_printf("<html><body style='font:16px sans-serif;margin:3em'><h2>Cannot open the page</h2>"
			       "<p>%s</p></body></html>", msg);
	webkit_web_view_load_alternate_html(web, html, uri, uri);
	g_free(msg);
	g_free(html);
	return TRUE;
}

static void on_mouse_target(WebKitWebView *web, WebKitHitTestResult *hit, guint modifiers, gpointer data)
{
	struct flo_page *p = data;
	(void)web; (void)modifiers;
	if (p->ev.hover != NULL)
		p->ev.hover(p->ui, webkit_hit_test_result_context_is_link(hit) ? webkit_hit_test_result_get_link_uri(hit) : NULL);
}

/* A page that wants a new window (window.open, target=_blank) gets none: there is one view per
 * window and no tab model yet. WebKit's default for a missing "create" handler is the same. */

struct flo_page *flo_page_new(const struct flo_page_events *ev, void *ui, int w, int h)
{
	struct flo_page *p = g_new0(struct flo_page, 1);
	WebKitColor white = { 1.0, 1.0, 1.0, 1.0 };

	p->ev = *ev;
	p->ui = ui;
	p->web = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
		"web-context", context, "network-session", session, "settings", settings,
		"display", display, NULL));
	webkit_web_view_set_background_color(p->web, &white);    /* opaque frames: the UI never blends */
	p->view = webkit_web_view_get_wpe_view(p->web);
	flo_view_set_frame_func(p->view, on_frame, p);

	g_signal_connect(p->web, "notify::title", G_CALLBACK(on_title), p);
	g_signal_connect(p->web, "notify::uri", G_CALLBACK(on_uri), p);
	g_signal_connect(p->web, "notify::estimated-load-progress", G_CALLBACK(on_progress), p);
	g_signal_connect(p->web, "load-changed", G_CALLBACK(on_load_changed), p);
	g_signal_connect(p->web, "load-failed", G_CALLBACK(on_load_failed), p);
	g_signal_connect(p->web, "mouse-target-changed", G_CALLBACK(on_mouse_target), p);

	flo_view_attach(p->view, w, h);
	return p;
}

void flo_page_free(struct flo_page *p)
{
	if (p == NULL)
		return;
	flo_view_set_frame_func(p->view, NULL, NULL);
	g_signal_handlers_disconnect_by_data(p->web, p);
	g_object_unref(p->web);
	g_free(p);
}

void flo_page_load(struct flo_page *p, const char *uri) { webkit_web_view_load_uri(p->web, uri); }
void flo_page_reload(struct flo_page *p) { webkit_web_view_reload(p->web); }
void flo_page_stop(struct flo_page *p) { webkit_web_view_stop_loading(p->web); }
void flo_page_back(struct flo_page *p) { webkit_web_view_go_back(p->web); }
void flo_page_forward(struct flo_page *p) { webkit_web_view_go_forward(p->web); }
bool flo_page_loading(struct flo_page *p) { return webkit_web_view_is_loading(p->web); }

void flo_page_resize(struct flo_page *p, int w, int h) { flo_view_set_size(p->view, w, h); }

const uint8_t *flo_page_pixels(struct flo_page *p, int *w, int *h, int *stride)
{
	return flo_view_pixels(p->view, w, h, stride);
}

/* ---- input --------------------------------------------------------------------------------------- */

static guint32 now_ms(void) { return (guint32)(g_get_monotonic_time() / 1000); }

static WPEModifiers wpe_mods(struct flo_page *p, int mods)
{
	unsigned m = p->buttons;

	if (mods & FLO_MOD_SHIFT) m |= WPE_MODIFIER_KEYBOARD_SHIFT;
	if (mods & FLO_MOD_CTRL) m |= WPE_MODIFIER_KEYBOARD_CONTROL;
	if (mods & FLO_MOD_ALT) m |= WPE_MODIFIER_KEYBOARD_ALT;
	if (mods & FLO_MOD_META) m |= WPE_MODIFIER_KEYBOARD_META;
	return (WPEModifiers)m;
}

static void send_event(struct flo_page *p, WPEEvent *e)
{
	wpe_view_event(p->view, e);
	wpe_event_unref(e);
}

void flo_page_pointer_move(struct flo_page *p, int mods, double x, double y)
{
	send_event(p, wpe_event_pointer_move_new(WPE_EVENT_POINTER_MOVE, p->view, WPE_INPUT_SOURCE_MOUSE,
		now_ms(), wpe_mods(p, mods), x, y, 0, 0));
}

void flo_page_pointer_button(struct flo_page *p, int mods, int button, bool down, int clicks, double x, double y)
{
	unsigned bit = button == WPE_BUTTON_PRIMARY ? WPE_MODIFIER_POINTER_BUTTON1
		     : button == WPE_BUTTON_MIDDLE ? WPE_MODIFIER_POINTER_BUTTON2 : WPE_MODIFIER_POINTER_BUTTON3;

	if (down)
		p->buttons |= bit;
	else
		p->buttons &= ~bit;
	send_event(p, wpe_event_pointer_button_new(down ? WPE_EVENT_POINTER_DOWN : WPE_EVENT_POINTER_UP, p->view,
		WPE_INPUT_SOURCE_MOUSE, now_ms(), wpe_mods(p, mods), (guint)button, x, y,
		down ? (clicks > 0 ? (guint)clicks : 1) : 0));      /* WPE asserts a press count only on a press */
}

void flo_page_scroll(struct flo_page *p, int mods, double dx, double dy, double x, double y)
{
	/* wheel notches, not pixels: WebKit scales a non-precise delta by the platform's line height */
	send_event(p, wpe_event_scroll_new(p->view, WPE_INPUT_SOURCE_MOUSE, now_ms(), wpe_mods(p, mods),
		dx, dy, FALSE, FALSE, x, y));
}

/* X11 keysyms, which is what WPE's keyval is. Latin-1 characters are their own keysym; everything
 * else is 0x01000000 + the Unicode code point. */
static guint keyval_of(uint32_t cp)
{
	if ((cp >= 0x20 && cp <= 0x7e) || (cp >= 0xa0 && cp <= 0xff))
		return cp;
	return 0x01000000u + cp;
}

static void send_key(struct flo_page *p, int mods, guint keyval, bool down)
{
	/* keycode 0: the hardware scancode is not known to the UI. Text and shortcuts go by keyval;
	 * KeyboardEvent.code in a page will be empty (to check on the first run). */
	send_event(p, wpe_event_keyboard_new(down ? WPE_EVENT_KEYBOARD_KEY_DOWN : WPE_EVENT_KEYBOARD_KEY_UP,
		p->view, WPE_INPUT_SOURCE_KEYBOARD, now_ms(), wpe_mods(p, mods), 0, keyval));
}

void flo_page_key(struct flo_page *p, int mods, uint32_t codepoint, bool down)
{
	send_key(p, mods, keyval_of(codepoint), down);
}

void flo_page_special_key(struct flo_page *p, int mods, int key, bool down)
{
	static const guint sym[] = {
		[FLO_KEY_ENTER] = 0xff0d, [FLO_KEY_TAB] = 0xff09, [FLO_KEY_BACKSPACE] = 0xff08,
		[FLO_KEY_DELETE] = 0xffff, [FLO_KEY_ESCAPE] = 0xff1b, [FLO_KEY_LEFT] = 0xff51,
		[FLO_KEY_UP] = 0xff52, [FLO_KEY_RIGHT] = 0xff53, [FLO_KEY_DOWN] = 0xff54,
		[FLO_KEY_HOME] = 0xff50, [FLO_KEY_END] = 0xff57, [FLO_KEY_PAGE_UP] = 0xff55,
		[FLO_KEY_PAGE_DOWN] = 0xff56,
	};
	if (key > 0 && (size_t)key < sizeof sym / sizeof sym[0] && sym[key] != 0)
		send_key(p, mods, sym[key], down);
}

void flo_page_focus(struct flo_page *p, bool focused)
{
	if (focused)
		wpe_view_focus_in(p->view);
	else
		wpe_view_focus_out(p->view);
}
