/*
 * Florence: the WPE platform (display, toplevel, view) that WebKit renders into. WPE WebKit lets an
 * embedder supply its own WPEDisplay; ours has no GPU, no compositor and no native surface: a
 * view receives each finished frame as a shared-memory buffer and hands it to the engine glue,
 * which tells the GNUstep UI what changed.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef FLORENCE_FLO_PLATFORM_H
#define FLORENCE_FLO_PLATFORM_H

#include <wpe/wpe-platform.h>

G_BEGIN_DECLS

WPEDisplay *flo_display_new(void);

/* The view's frame sink: called (from the main loop, never re-entrantly) with the bounding box of
 * what changed since the last frame, once the new frame is the current one. */
typedef void (*FloFrameFunc)(WPEView *view, int x, int y, int w, int h, gpointer data);
void flo_view_set_frame_func(WPEView *view, FloFrameFunc func, gpointer data);

/* The current frame's pixels (BGRA, premultiplied, rows top-down), or NULL before the first. */
const guint8 *flo_view_pixels(WPEView *view, int *width, int *height, int *stride);

/* Make `view` a full-size toplevel of its own, of the given size, and show it. */
void flo_view_attach(WPEView *view, int width, int height);
void flo_view_set_size(WPEView *view, int width, int height);

G_END_DECLS

#endif
