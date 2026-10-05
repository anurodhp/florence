/*
 * Florence: draw the engine's frame through cairo directly. gnustep-gui's own image drawing copies and converts every pixel
 * of the image (and composites it with EXTEND_PAD through pixman's slow general path); the engine's buffer is already what cairo
 * wants (BGRA, 32 bits a pixel: CAIRO_FORMAT_RGB24), so wrapping it in a cairo surface and filling the exposed rectangle is a
 * blit. It reaches under gnustep-back: the current graphics state's cairo_t and CTM are read by instance-variable name
 * (GSContext.gstate, GSGState.ctm, CairoGState._ct), and cairo's functions come from the libcairo gnustep-back already loaded.
 * Anything missing and FloCairoDraw() returns NO; the caller draws the old way.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#import <AppKit/AppKit.h>

/* Draw `dirty` (view coordinates, flipped view, the frame's top-left at the view's origin) of a w x h frame. */
BOOL FloCairoDraw(const unsigned char *bgra, int stride, int w, int h, NSRect dirty);
