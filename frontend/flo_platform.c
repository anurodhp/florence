/*
 * Florence: the WPE platform. See flo_platform.h. Modelled on WebKit's own headless platform
 * (Source/WebKit/WPEPlatform/wpe/headless/, the pinned tree's reference for what a minimal
 * display must implement), with the frame timer replaced by a hand-over to the UI.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#include "flo_platform.h"

#include <string.h>

/* ---- toplevel: a size and an "active" state, nothing native ------------------------------------ */

#define FLO_TYPE_TOPLEVEL (flo_toplevel_get_type())
G_DECLARE_FINAL_TYPE(FloToplevel, flo_toplevel, FLO, TOPLEVEL, WPEToplevel)
struct _FloToplevel { WPEToplevel parent; };
G_DEFINE_FINAL_TYPE(FloToplevel, flo_toplevel, WPE_TYPE_TOPLEVEL)

static void flo_toplevel_constructed(GObject *object)
{
	G_OBJECT_CLASS(flo_toplevel_parent_class)->constructed(object);
	wpe_toplevel_state_changed(WPE_TOPLEVEL(object), WPE_TOPLEVEL_STATE_ACTIVE);
}

static gboolean tell_view_size(WPEToplevel *toplevel, WPEView *view, gpointer data)
{
	int w, h;
	(void)data;
	wpe_toplevel_get_size(toplevel, &w, &h);
	wpe_view_resized(view, w, h);
	return FALSE;                   /* keep going */
}

static gboolean flo_toplevel_resize(WPEToplevel *toplevel, int width, int height)
{
	wpe_toplevel_resized(toplevel, width, height);
	wpe_toplevel_foreach_view(toplevel, tell_view_size, NULL);
	return TRUE;
}

static void flo_toplevel_class_init(FloToplevelClass *klass)
{
	G_OBJECT_CLASS(klass)->constructed = flo_toplevel_constructed;
	WPE_TOPLEVEL_CLASS(klass)->resize = flo_toplevel_resize;
}

static void flo_toplevel_init(FloToplevel *self) { (void)self; }

/* ---- view: receives frames ---------------------------------------------------------------------- */

#define FLO_TYPE_VIEW (flo_view_get_type())
G_DECLARE_FINAL_TYPE(FloView, flo_view, FLO, VIEW, WPEView)
struct _FloView {
	WPEView parent;
	WPEBuffer *committed;           /* the frame the UI is showing */
	WPEBuffer *pending;             /* handed over by WebKit, not yet current */
	gboolean whole, have_box;       /* the pending frame's damage: everything, or the box below */
	int bx0, by0, bx1, by1;
	guint idle;                     /* source id of the commit callback, 0 if none */
	FloFrameFunc frame_func;
	gpointer frame_data;
};
G_DEFINE_FINAL_TYPE(FloView, flo_view, WPE_TYPE_VIEW)

static gboolean view_commit(gpointer data)
{
	FloView *self = FLO_VIEW(data);
	WPEView *view = WPE_VIEW(self);
	WPEBuffer *old = self->committed;
	int x = 0, y = 0, w, h;

	self->idle = 0;
	if (self->pending == NULL)
		return G_SOURCE_REMOVE;
	self->committed = self->pending;
	self->pending = NULL;
	w = wpe_buffer_get_width(self->committed);
	h = wpe_buffer_get_height(self->committed);
	if (!self->whole && self->have_box) {
		x = self->bx0; y = self->by0;
		w = self->bx1 - self->bx0; h = self->by1 - self->by0;
	}
	self->whole = FALSE;
	self->have_box = FALSE;

	if (self->frame_func != NULL)
		self->frame_func(view, x, y, w, h, self->frame_data);
	/* the order WebKit's own headless view uses: the previous frame is free, the new one is shown */
	if (old != NULL) {
		wpe_view_buffer_released(view, old);
		g_object_unref(old);
	}
	wpe_view_buffer_rendered(view, self->committed);
	return G_SOURCE_REMOVE;
}

guint64 flo_frames_total;
gint64 flo_last_frame_us;
gint64 flo_frame_times_us[FLO_FRAME_LOG];

/* FLORENCE_STATS=1: frames per second and the mean damaged area, to stderr every 5 s, also when nothing arrived
 * (is something repainting while the page is idle?) */
static guint stat_frames;
static double stat_area;

static gboolean stats_tick(gpointer data)
{
	(void)data;
	g_printerr("florence: %.1f frames/s, mean damage %.0f px\n", stat_frames / 5.0, stat_frames ? stat_area / stat_frames : 0.0);
	stat_frames = 0; stat_area = 0;
	return G_SOURCE_CONTINUE;
}

static void frame_stats(const WPERectangle *damage, guint n_damage)
{
	static int enabled = -1;
	guint i;

	if (enabled < 0) {
		enabled = g_getenv("FLORENCE_STATS") != NULL;
		if (enabled)
			g_timeout_add_seconds(5, stats_tick, NULL);
	}
	if (!enabled)
		return;
	stat_frames++;
	for (i = 0; i < n_damage && damage != NULL; i++)
		stat_area += (double)damage[i].width * damage[i].height;
}

static gboolean flo_view_render_buffer(WPEView *view, WPEBuffer *buffer, const WPERectangle *damage,
				       guint n_damage, GError **error)
{
	FloView *self = FLO_VIEW(view);
	guint i;

	flo_last_frame_us = g_get_monotonic_time();
	flo_frame_times_us[flo_frames_total % FLO_FRAME_LOG] = flo_last_frame_us;
	flo_frames_total++;
	frame_stats(damage, n_damage);

	if (!WPE_IS_BUFFER_SHM(buffer)) {
		g_set_error_literal(error, WPE_VIEW_ERROR, WPE_VIEW_ERROR_RENDER_FAILED,
				    "Florence draws shared-memory buffers only");
		return FALSE;
	}
	/* WebKit sends the next frame only after buffer-rendered for this one, so `pending` is normally
	 * empty here; if not, the newer frame simply replaces it (as the headless view does). */
	g_clear_object(&self->pending);
	self->pending = g_object_ref(buffer);

	if (n_damage == 0 || damage == NULL)
		self->whole = TRUE;
	for (i = 0; i < n_damage && damage != NULL; i++) {
		int x0 = damage[i].x, y0 = damage[i].y;
		int x1 = x0 + damage[i].width, y1 = y0 + damage[i].height;
		if (!self->have_box) {
			self->bx0 = x0; self->by0 = y0; self->bx1 = x1; self->by1 = y1;
			self->have_box = TRUE;
		} else {
			if (x0 < self->bx0) self->bx0 = x0;
			if (y0 < self->by0) self->by0 = y0;
			if (x1 > self->bx1) self->bx1 = x1;
			if (y1 > self->by1) self->by1 = y1;
		}
	}
	/* never re-enter WebKit from inside its own render call: commit from the main loop */
	if (self->idle == 0)
		self->idle = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, view_commit, g_object_ref(self), g_object_unref);
	return TRUE;
}

static void flo_view_dispose(GObject *object)
{
	FloView *self = FLO_VIEW(object);

	if (self->idle != 0) {
		g_source_remove(self->idle);    /* drops the reference the source holds */
		self->idle = 0;
	}
	g_clear_object(&self->pending);
	g_clear_object(&self->committed);
	self->frame_func = NULL;
	G_OBJECT_CLASS(flo_view_parent_class)->dispose(object);
}

static void flo_view_class_init(FloViewClass *klass)
{
	G_OBJECT_CLASS(klass)->dispose = flo_view_dispose;
	WPE_VIEW_CLASS(klass)->render_buffer = flo_view_render_buffer;
}

static void flo_view_init(FloView *self) { (void)self; }

void flo_view_set_frame_func(WPEView *view, FloFrameFunc func, gpointer data)
{
	FloView *self = FLO_VIEW(view);
	self->frame_func = func;
	self->frame_data = data;
}

const guint8 *flo_view_pixels(WPEView *view, int *width, int *height, int *stride)
{
	FloView *self = FLO_VIEW(view);
	WPEBufferSHM *shm;

	if (self->committed == NULL || !WPE_IS_BUFFER_SHM(self->committed))
		return NULL;
	shm = WPE_BUFFER_SHM(self->committed);
	*width = wpe_buffer_get_width(self->committed);
	*height = wpe_buffer_get_height(self->committed);
	*stride = (int)wpe_buffer_shm_get_stride(shm);
	return g_bytes_get_data(wpe_buffer_shm_get_data(shm), NULL);
}

void flo_view_attach(WPEView *view, int width, int height)
{
	WPEToplevel *top = wpe_display_create_toplevel(wpe_view_get_display(view), 1);

	wpe_toplevel_resize(top, width, height);        /* before the view joins: it is told on notify::toplevel */
	wpe_view_set_toplevel(view, top);
	g_object_unref(top);                            /* the view holds it */
	wpe_view_resized(view, width, height);
	wpe_view_map(view);
	wpe_view_focus_in(view);
}

void flo_view_set_size(WPEView *view, int width, int height)
{
	WPEToplevel *top = wpe_view_get_toplevel(view);

	if (top != NULL)
		wpe_toplevel_resize(top, width, height);
}

/* ---- display ------------------------------------------------------------------------------------ */

#define FLO_TYPE_DISPLAY (flo_display_get_type())
G_DECLARE_FINAL_TYPE(FloDisplay, flo_display, FLO, DISPLAY, WPEDisplay)
struct _FloDisplay { WPEDisplay parent; };
G_DEFINE_FINAL_TYPE(FloDisplay, flo_display, WPE_TYPE_DISPLAY)

static gboolean flo_display_connect(WPEDisplay *display, GError **error)
{
	(void)display; (void)error;
	return TRUE;                    /* there is nothing to connect to */
}

static WPEView *flo_display_create_view(WPEDisplay *display)
{
	return WPE_VIEW(g_object_new(FLO_TYPE_VIEW, "display", display, NULL));
}

static WPEToplevel *flo_display_create_toplevel(WPEDisplay *display, guint max_views)
{
	(void)max_views;
	return WPE_TOPLEVEL(g_object_new(FLO_TYPE_TOPLEVEL, "display", display, NULL));
}

/* get_egl_display is deliberately not implemented: no EGL display means WebKit never creates a GL
 * context in this process, and (with WEBKIT_SKIA_ENABLE_CPU_RENDERING) the web process paints into
 * shared memory. GPU support later is a get_egl_display and a DMA-BUF buffer path, here. */

static void flo_display_class_init(FloDisplayClass *klass)
{
	WPEDisplayClass *dc = WPE_DISPLAY_CLASS(klass);

	dc->connect = flo_display_connect;
	dc->create_view = flo_display_create_view;
	dc->create_toplevel = flo_display_create_toplevel;
}

static void flo_display_init(FloDisplay *self) { (void)self; }

WPEDisplay *flo_display_new(void)
{
	return WPE_DISPLAY(g_object_new(FLO_TYPE_DISPLAY, NULL));
}
