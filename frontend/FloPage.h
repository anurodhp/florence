/*
 * Florence: the page view. A flipped NSView, the document view of an
 * NSScrollView, as big as the page; drawRect: paints only the dirty rectangle
 * (the part AppKit says is exposed) through NetSurf's cairo plotters into one
 * small reusable buffer. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#import <AppKit/AppKit.h>
#include "gnustep/gs.h"

@interface FloPage : NSView {
	struct gui_window *gw;
	void *surface;                  /* cairo_surface_t*, RGB24, grown never shrunk */
	int surfW, surfH;
	unsigned char *rgb;             /* the same pixels as 24-bit RGB for AppKit */
	size_t rgbCap;
	NSRect caret;
	BOOL hasCaret;
	BOOL dragging, pressed;
	NSPoint pressPoint;
	NSCursor *cursor;
}
- (id)initWithGuiWindow:(struct gui_window *)g;
- (void)detach;                         /* the core is done with the window: stop calling it */
- (void)invalidatePageRect:(NSRect)r;
- (void)placeCaret:(NSRect)r;
- (void)removeCaret;
- (void)setPointer:(int)flo_pointer;
@end
