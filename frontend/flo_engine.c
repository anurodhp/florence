/*
 * Florence: the engine glue. WPE WebKit (software rendering, no GPU) behind the plain C interface in
 * flo.h. This file and flo_platform.c are the only ones that include WebKit or GLib headers.
 *
 * Small-machine rules, nothing runs while the page is
 * idle (GLib's main context is driven from the UI's run loop and sleeps in it, see flo_glib_*),
 * frames are capped at 30 per second and arrive as damage rectangles, caches are the smallest
 * WebKit offers.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#include "flo.h"
#include "flo_platform.h"

#include <glib.h>
#include <stdio.h>
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
static WebKitUserContentManager *content;   /* shared too: the content blocker's rules */
static WebKitUserContentFilterStore *filter_store;
static WebKitUserContentFilter *filter;     /* the compiled default list, once WebKit has it */
static char *data_path;

static void setdefault(const char *name, const char *value) { setenv(name, value, 0); }

static void on_download_started(WebKitNetworkSession *s, WebKitDownload *d, gpointer data);

static void apply_font_min(void)
{
	int tenths = flo_opt_font_min();

	/* WebKit's minimum is in CSS pixels (96 per inch); 0 is "no minimum". 8.5 pt is the old default and means none. */
	webkit_settings_set_minimum_font_size(settings, tenths <= 85 ? 0 : (tenths * 96 + 360) / 720);
}

int flo_engine_init(const char *data_dir, const char *cache_dir)
{
	GError *error = NULL;

	/* Read by WebKit's processes at start-up (they inherit our environment); setdefault keeps
	 * anything the user put there for experiments. Each is a source line in the pinned tree: */
	/* FLORENCE_GL=1: the GL path (WebKit's GL compositor and Skia's GL backend, through libepoxy and compat/egl on the Mesa GLX
	 * presenter, frames read back into shared memory). Default is the CPU path until the GL path measures faster. */
	if (getenv("FLORENCE_GL") == NULL) {
		setdefault("WEBKIT_DISABLE_COMPOSITING_MODE", "1");      /* scripts/webkit_fixes.sh: no GL compositor, paint on the CPU */
		setdefault("WEBKIT_SKIA_ENABLE_CPU_RENDERING", "1");     /* WebProcessGLib.cpp: no GPU buffers, shared memory */
	}
	setdefault("WEBKIT_SKIA_CPU_PAINTING_THREADS", "1");     /* SkiaPaintingEngine.cpp: default is half the cores */
	setdefault("WEBKIT_FORCE_VBLANK_TIMER", "1");            /* DisplayVBlankMonitor.cpp: no screen to ask */
	setdefault("WEBKIT_DISPLAY_REFRESH_THROTTLE_FPS", "30"); /* DisplayLinkGLib.cpp: a factor of the 60 Hz timer */

	setdefault("GIO_EXTRA_MODULES", "/usr/local/lib/gio/modules");   /* glib giomodule.c: the openssl TLS backend (scripts/build_meson_lib.sh gnet) */
	setdefault("SSL_CERT_FILE", "/usr/local/ssl/cert.pem");  /* OpenSSL crypto/x509/by_file.c: the CA roots staged by build_openssl.sh */

	display = flo_display_new();
	if (!wpe_display_connect(display, &error)) {
		g_printerr("florence: display: %s\n", error->message);
		g_error_free(error);
		return -1;
	}
	wpe_display_set_primary(display);

	session = webkit_network_session_new(data_dir, cache_dir);
	g_signal_connect(session, "download-started", G_CALLBACK(on_download_started), NULL);
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
	webkit_settings_set_enable_javascript(settings, strcmp(flo_pref_get("javascript", "1"), "1") == 0);
	apply_font_min();
	content = webkit_user_content_manager_new();
	data_path = g_strdup(data_dir);
	return 0;
}

void flo_engine_fini(void)
{
	g_clear_pointer(&filter, webkit_user_content_filter_unref);
	g_clear_object(&filter_store);
	g_clear_object(&content);
	g_clear_pointer(&data_path, g_free);
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
	flo_pref_set("javascript", on ? "1" : "0");
}

void flo_opt_set_font_min(int tenths)
{
	char v[16];

	snprintf(v, sizeof v, "%d", tenths);
	flo_pref_set("font_min", v);
	if (settings != NULL)
		apply_font_min();
}

void flo_clear_cookies(void)
{
	if (session != NULL)
		webkit_website_data_manager_clear(webkit_network_session_get_website_data_manager(session),
			WEBKIT_WEBSITE_DATA_COOKIES, 0, NULL, NULL, NULL);
}

/* ---- the clipboard ------------------------------------------------------------------------------- */

int64_t flo_clipboard_count(void)
{
	WPEClipboard *c = display != NULL ? wpe_display_get_clipboard(display) : NULL;
	return c != NULL ? wpe_clipboard_get_change_count(c) : 0;
}

char *flo_clipboard_text(void)
{
	WPEClipboard *c = display != NULL ? wpe_display_get_clipboard(display) : NULL;
	gsize size = 0;
	char *t = c != NULL ? wpe_clipboard_read_text(c, "text/plain;charset=utf-8", &size) : NULL;
	char *out = t != NULL ? strdup(t) : NULL;

	g_free(t);
	return out;
}

void flo_clipboard_set_text(const char *text)
{
	WPEClipboard *c = display != NULL ? wpe_display_get_clipboard(display) : NULL;
	WPEClipboardContent *content;

	if (c == NULL || text == NULL)
		return;
	content = wpe_clipboard_content_new();
	wpe_clipboard_content_set_text(content, text);
	wpe_clipboard_set_content(c, content);
	wpe_clipboard_content_unref(content);
}

/* ---- downloads ------------------------------------------------------------------------------------ */

static flo_download_func download_func;
static void *download_ctx;

void flo_engine_set_download_handler(flo_download_func func, void *ctx)
{
	download_func = func;
	download_ctx = ctx;
}

static const char *download_name(WebKitDownload *d)
{
	const char *dest = webkit_download_get_destination(d);
	const char *slash = dest != NULL ? strrchr(dest, '/') : NULL;
	return slash != NULL ? slash + 1 : (dest != NULL ? dest : "");
}

static gboolean on_decide_destination(WebKitDownload *d, const char *suggested, gpointer data)
{
	const char *pref = flo_pref_get("downloads", "");
	char *dir = pref[0] != '\0' ? g_strdup(pref) : g_build_filename(g_get_home_dir(), "Downloads", NULL);
	char *base = g_path_get_basename(suggested != NULL && suggested[0] != '\0' ? suggested : "download");
	char *path;
	int n = 0;
	(void)data;

	g_mkdir_with_parents(dir, 0755);
	path = g_build_filename(dir, base, NULL);
	while (g_file_test(path, G_FILE_TEST_EXISTS)) {          /* never overwrite: name (2).ext */
		char *dot = strrchr(base, '.'), *name;
		g_free(path);
		name = dot != NULL && dot != base
			? g_strdup_printf("%.*s (%d)%s", (int)(dot - base), base, ++n + 1, dot)
			: g_strdup_printf("%s (%d)", base, ++n + 1);
		path = g_build_filename(dir, name, NULL);
		g_free(name);
	}
	webkit_download_set_destination(d, path);
	g_free(path);
	g_free(base);
	g_free(dir);
	return TRUE;
}

static void on_download_progress(GObject *o, GParamSpec *spec, gpointer data)
{
	WebKitDownload *d = WEBKIT_DOWNLOAD(o);
	(void)spec; (void)data;
	if (download_func != NULL)
		download_func(download_ctx, FLO_DOWNLOAD_PROGRESS, download_name(d), webkit_download_get_estimated_progress(d));
}

static void on_download_finished(WebKitDownload *d, gpointer data)
{
	(void)data;
	if (download_func != NULL)
		download_func(download_ctx, FLO_DOWNLOAD_FINISHED, download_name(d), 1.0);
}

static void on_download_failed(WebKitDownload *d, GError *error, gpointer data)
{
	(void)error; (void)data;
	if (download_func != NULL)
		download_func(download_ctx, FLO_DOWNLOAD_FAILED, download_name(d), 0.0);
}

static void on_download_started(WebKitNetworkSession *s, WebKitDownload *d, gpointer data)
{
	(void)s; (void)data;
	g_signal_connect(d, "decide-destination", G_CALLBACK(on_decide_destination), NULL);
	g_signal_connect(d, "notify::estimated-progress", G_CALLBACK(on_download_progress), NULL);
	g_signal_connect(d, "finished", G_CALLBACK(on_download_finished), NULL);
	g_signal_connect(d, "failed", G_CALLBACK(on_download_failed), NULL);
	if (download_func != NULL)
		download_func(download_ctx, FLO_DOWNLOAD_STARTED, "", 0.0);
}

/* ---- the content blocker ------------------------------------------------------------------------- */

/* Rules are Safari's content-blocker JSON, which WebKit compiles natively (ENABLE_CONTENT_EXTENSIONS); the user content manager is
 * shared by every page, so one add or remove covers all of them. A page applies the rules when it next loads. */
static void apply_blocker(void)
{
	if (content == NULL)
		return;
	webkit_user_content_manager_remove_all_filters(content);
	if (flo_opt_hide_ads() && filter != NULL)
		webkit_user_content_manager_add_filter(content, filter);
}

void flo_opt_set_hide_ads(bool on)
{
	flo_pref_set("hide_ads", on ? "1" : "0");
	apply_blocker();
}

static void filter_ready(GObject *o, GAsyncResult *res, gpointer data)
{
	GError *error = NULL;
	WebKitUserContentFilter *f = webkit_user_content_filter_store_save_from_file_finish(WEBKIT_USER_CONTENT_FILTER_STORE(o), res, &error);
	(void)data;

	if (f == NULL) {
		g_printerr("florence: content blocker: %s\n", error != NULL ? error->message : "failed");
		g_clear_error(&error);
		return;
	}
	g_clear_pointer(&filter, webkit_user_content_filter_unref);
	filter = f;
	apply_blocker();
}

static void filter_load_done(GObject *o, GAsyncResult *res, gpointer data)
{
	char *json = data;
	GError *error = NULL;
	WebKitUserContentFilter *f = webkit_user_content_filter_store_load_finish(WEBKIT_USER_CONTENT_FILTER_STORE(o), res, &error);

	g_clear_error(&error);
	if (f != NULL) {                /* compiled by an earlier run */
		g_clear_pointer(&filter, webkit_user_content_filter_unref);
		filter = f;
		apply_blocker();
	} else {
		GFile *file = g_file_new_for_path(json);
		webkit_user_content_filter_store_save_from_file(WEBKIT_USER_CONTENT_FILTER_STORE(o), "florence-default-1", file, NULL,
			filter_ready, NULL);
		g_object_unref(file);
	}
	g_free(json);
}

void flo_engine_set_blocklist(const char *json_path)
{
	char *dir;

	if (data_path == NULL || filter_store != NULL)
		return;
	dir = g_build_filename(data_path, "filters", NULL);
	g_mkdir_with_parents(dir, 0700);
	filter_store = webkit_user_content_filter_store_new(dir);
	g_free(dir);
	webkit_user_content_filter_store_load(filter_store, "florence-default-1", NULL, filter_load_done, g_strdup(json_path));
}

/* ---- pages --------------------------------------------------------------------------------------- */

struct flo_page {
	WebKitWebView *web;
	WPEView *view;
	struct flo_page_events ev;
	void *ui;
	unsigned buttons;               /* pointer buttons held, as WPE modifier bits */
	double zoom;
	char *hit_link, *hit_image;     /* the last mouse-target-changed */
	bool hit_editable, hit_selection;
	char *find_text;                /* the text of the search in progress, to tell "next" from "a new search" */
	bool find_cs;
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

/* The web process died (or was killed): say so on stderr (the log of the app), since the page just stops otherwise. */
static void on_web_process_terminated(WebKitWebView *web, WebKitWebProcessTerminationReason reason, gpointer data)
{
	(void)web; (void)data;
	g_printerr("florence: web process terminated: %s\n", reason == WEBKIT_WEB_PROCESS_CRASHED ? "crashed"
		: reason == WEBKIT_WEB_PROCESS_EXCEEDED_MEMORY_LIMIT ? "exceeded its memory limit" : "terminated by the API");
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
	if (event == WEBKIT_LOAD_STARTED && p->ev.loading != NULL)
		p->ev.loading(p->ui, true);
	if (event == WEBKIT_LOAD_FINISHED && p->ev.loading != NULL)
		p->ev.loading(p->ui, false);
	if (event == WEBKIT_LOAD_FINISHED && p->ev.progress != NULL)
		p->ev.progress(p->ui, 1.0);
	if (event == WEBKIT_LOAD_COMMITTED || event == WEBKIT_LOAD_FINISHED)
		report_nav(p);
}

static gboolean on_load_failed(WebKitWebView *web, WebKitLoadEvent event, const char *uri, GError *error, gpointer data)
{
	char *msg, *html;
	(void)event;
	if (g_error_matches(error, WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED)) {
		struct flo_page *p = data;
		if (p->ev.loading != NULL)
			p->ev.loading(p->ui, false);
		return FALSE;           /* the user stopped it, or navigated away */
	}
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
	g_free(p->hit_link);
	g_free(p->hit_image);
	p->hit_link = webkit_hit_test_result_context_is_link(hit) ? g_strdup(webkit_hit_test_result_get_link_uri(hit)) : NULL;
	p->hit_image = webkit_hit_test_result_context_is_image(hit) ? g_strdup(webkit_hit_test_result_get_image_uri(hit)) : NULL;
	p->hit_editable = webkit_hit_test_result_context_is_editable(hit);
	p->hit_selection = webkit_hit_test_result_context_is_selection(hit);
	if (p->ev.hover != NULL)
		p->ev.hover(p->ui, p->hit_link);
}

/* A link that wants a new window (target=_blank, a middle click) is handed to the UI as a new tab. window.open() from a script
 * still gets no window: WebKit's default for a missing "create" handler. */
static gboolean on_decide_policy(WebKitWebView *web, WebKitPolicyDecision *decision, WebKitPolicyDecisionType type, gpointer data)
{
	struct flo_page *p = data;
	(void)web;
	if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
		WebKitResponsePolicyDecision *rd = WEBKIT_RESPONSE_POLICY_DECISION(decision);
		WebKitURIResponse *resp = webkit_response_policy_decision_get_response(rd);
		SoupMessageHeaders *headers = webkit_uri_response_get_http_headers(resp);
		const char *disp = headers != NULL ? soup_message_headers_get_one(headers, "Content-Disposition") : NULL;

		if (!webkit_response_policy_decision_is_mime_type_supported(rd) || (disp != NULL && g_ascii_strncasecmp(disp, "attachment", 10) == 0)) {
			webkit_policy_decision_download(decision);
			return TRUE;
		}
		return FALSE;
	}
	if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
		WebKitNavigationAction *a = webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
		WebKitURIRequest *r = webkit_navigation_action_get_request(a);
		if (p->ev.open_tab != NULL && r != NULL)
			p->ev.open_tab(p->ui, webkit_uri_request_get_uri(r));
		webkit_policy_decision_ignore(decision);
		return TRUE;
	}
	return FALSE;
}

static void on_found(WebKitFindController *fc, guint count, gpointer data)
{
	struct flo_page *p = data;
	(void)fc; (void)count;
	if (p->ev.found != NULL)
		p->ev.found(p->ui, true);
}

static void on_not_found(WebKitFindController *fc, gpointer data)
{
	struct flo_page *p = data;
	(void)fc;
	if (p->ev.found != NULL)
		p->ev.found(p->ui, false);
}

struct flo_page *flo_page_new(const struct flo_page_events *ev, void *ui, int w, int h)
{
	struct flo_page *p = g_new0(struct flo_page, 1);
	WebKitColor white = { 1.0, 1.0, 1.0, 1.0 };

	p->ev = *ev;
	p->ui = ui;
	p->web = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
		"web-context", context, "network-session", session, "settings", settings,
		"user-content-manager", content, "display", display, NULL));
	p->zoom = 1.0;
	webkit_web_view_set_background_color(p->web, &white);    /* opaque frames: the UI never blends */
	p->view = webkit_web_view_get_wpe_view(p->web);
	flo_view_set_frame_func(p->view, on_frame, p);

	g_signal_connect(p->web, "web-process-terminated", G_CALLBACK(on_web_process_terminated), p);
	g_signal_connect(p->web, "notify::title", G_CALLBACK(on_title), p);
	g_signal_connect(p->web, "notify::uri", G_CALLBACK(on_uri), p);
	g_signal_connect(p->web, "notify::estimated-load-progress", G_CALLBACK(on_progress), p);
	g_signal_connect(p->web, "load-changed", G_CALLBACK(on_load_changed), p);
	g_signal_connect(p->web, "load-failed", G_CALLBACK(on_load_failed), p);
	g_signal_connect(p->web, "mouse-target-changed", G_CALLBACK(on_mouse_target), p);
	g_signal_connect(p->web, "decide-policy", G_CALLBACK(on_decide_policy), p);
	g_signal_connect(webkit_web_view_get_find_controller(p->web), "found-text", G_CALLBACK(on_found), p);
	g_signal_connect(webkit_web_view_get_find_controller(p->web), "failed-to-find-text", G_CALLBACK(on_not_found), p);

	flo_view_attach(p->view, w, h);
	return p;
}

void flo_page_free(struct flo_page *p)
{
	if (p == NULL)
		return;
	flo_view_set_frame_func(p->view, NULL, NULL);
	g_signal_handlers_disconnect_by_data(p->web, p);
	g_signal_handlers_disconnect_by_data(webkit_web_view_get_find_controller(p->web), p);
	g_object_unref(p->web);
	g_free(p->find_text);
	g_free(p->hit_link);
	g_free(p->hit_image);
	g_free(p);
}

void flo_page_load(struct flo_page *p, const char *uri) { webkit_web_view_load_uri(p->web, uri); }
void flo_page_reload(struct flo_page *p) { webkit_web_view_reload(p->web); }
void flo_page_stop(struct flo_page *p) { webkit_web_view_stop_loading(p->web); }
void flo_page_back(struct flo_page *p) { webkit_web_view_go_back(p->web); }
void flo_page_forward(struct flo_page *p) { webkit_web_view_go_forward(p->web); }
bool flo_page_loading(struct flo_page *p) { return webkit_web_view_is_loading(p->web); }

int flo_page_security(struct flo_page *p)
{
	GTlsCertificate *cert = NULL;
	GTlsCertificateFlags errors = 0;

	return webkit_web_view_get_tls_info(p->web, &cert, &errors) && errors == 0 ? 1 : 0;
}

void flo_page_find(struct flo_page *p, const char *text, bool forwards, bool case_sensitive)
{
	WebKitFindController *fc = webkit_web_view_get_find_controller(p->web);
	guint opts = WEBKIT_FIND_OPTIONS_WRAP_AROUND | (case_sensitive ? 0 : WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE)
		   | (forwards ? 0 : WEBKIT_FIND_OPTIONS_BACKWARDS);

	if (text == NULL || text[0] == '\0') {
		webkit_find_controller_search_finish(fc);
		g_clear_pointer(&p->find_text, g_free);
		return;
	}
	if (p->find_text != NULL && strcmp(p->find_text, text) == 0 && p->find_cs == case_sensitive) {
		if (forwards)
			webkit_find_controller_search_next(fc);
		else
			webkit_find_controller_search_previous(fc);
		return;
	}
	g_free(p->find_text);
	p->find_text = g_strdup(text);
	p->find_cs = case_sensitive;
	webkit_find_controller_search(fc, text, opts, G_MAXUINT);
}

void flo_page_find_clear(struct flo_page *p)
{
	webkit_find_controller_search_finish(webkit_web_view_get_find_controller(p->web));
	g_clear_pointer(&p->find_text, g_free);
}

int flo_page_zoom(struct flo_page *p, int step)
{
	double z = step == 0 ? 1.0 : p->zoom + step * 0.1;

	if (z < 0.3) z = 0.3;
	if (z > 3.0) z = 3.0;
	p->zoom = (int)(z * 10 + 0.5) / 10.0;
	webkit_web_view_set_zoom_level(p->web, p->zoom);
	return (int)(p->zoom * 100 + 0.5);
}

void flo_page_hit(struct flo_page *p, struct flo_hit *h)
{
	h->link = p->hit_link;
	h->image = p->hit_image;
	h->editable = p->hit_editable;
	h->selection = p->hit_selection;
}

void flo_page_edit(struct flo_page *p, int edit)
{
	static const char *const command[] = { NULL, WEBKIT_EDITING_COMMAND_CUT, WEBKIT_EDITING_COMMAND_COPY,
					       WEBKIT_EDITING_COMMAND_PASTE_AS_PLAIN_TEXT, WEBKIT_EDITING_COMMAND_SELECT_ALL };

	if (edit > 0 && (size_t)edit < sizeof command / sizeof command[0])
		webkit_web_view_execute_editing_command(p->web, command[edit]);
}

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
