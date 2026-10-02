/* Florence: the page view. See FloPage.h. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only */
#import "FloPage.h"
#include <cairo.h>
#include <math.h>
#include <stdlib.h>

#define DRAG_SLOP 5.0

@implementation FloPage

- (id)initWithGuiWindow:(struct gui_window *)g
{
	if ((self = [super initWithFrame:NSMakeRect(0, 0, 100, 100)]) != nil)
		gw = g;
	return self;
}

- (void)dealloc
{
	if (surface != NULL)
		cairo_surface_destroy(surface);
	free(rgb);
	[cursor release];
	[super dealloc];
}

- (void)detach { gw = NULL; }
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }

/* ---- drawing ------------------------------------------------------------- */

- (BOOL)ensureBufferW:(int)w H:(int)h
{
	if (surface == NULL || w > surfW || h > surfH) {
		int nw = w > surfW ? w : surfW, nh = h > surfH ? h : surfH;
		cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, nw, nh);
		unsigned char *p;
		if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
			cairo_surface_destroy(s);
			return NO;
		}
		p = realloc(rgb, (size_t)nw * nh * 3);
		if (p == NULL) {
			cairo_surface_destroy(s);
			return NO;
		}
		rgb = p;
		if (surface != NULL)
			cairo_surface_destroy(surface);
		surface = s;
		surfW = nw;
		surfH = nh;
	}
	return YES;
}

- (void)drawRect:(NSRect)dirty
{
	NSRect r = NSIntersectionRect(dirty, [self visibleRect]);
	int x0 = (int)floor(NSMinX(r)), y0 = (int)floor(NSMinY(r));
	int x1 = (int)ceil(NSMaxX(r)), y1 = (int)ceil(NSMaxY(r));
	int w = x1 - x0, h = y1 - y0, y;

	if (gw == NULL || w <= 0 || h <= 0 || ![self ensureBufferW:w H:h]) {
		[[NSColor whiteColor] set];
		NSRectFill(dirty);
		return;
	}

	cairo_t *cr = cairo_create(surface);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_clip(cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_paint(cr);
	cairo_translate(cr, -x0, -y0);         /* the plotters draw in page coordinates */
	flo_win_redraw(gw, cr, x0, y0, x1, y1);
	cairo_destroy(cr);
	cairo_surface_flush(surface);

	/* cairo RGB24 is B,G,R,x in memory (little endian); AppKit wants R,G,B */
	const unsigned char *src = cairo_image_surface_get_data(surface);
	int stride = cairo_image_surface_get_stride(surface);
	for (y = 0; y < h; y++) {
		const unsigned char *s = src + (size_t)y * stride;
		unsigned char *d = rgb + (size_t)y * w * 3;
		int x;
		for (x = 0; x < w; x++, s += 4, d += 3) {
			d[0] = s[2];
			d[1] = s[1];
			d[2] = s[0];
		}
	}

	unsigned char *planes[1] = { rgb };
	NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:planes
		pixelsWide:w pixelsHigh:h bitsPerSample:8 samplesPerPixel:3 hasAlpha:NO isPlanar:NO
		colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:w * 3 bitsPerPixel:24];
	NSImage *img = [[NSImage alloc] initWithSize:NSMakeSize(w, h)];
	[img addRepresentation:rep];
	/* The rows are top-down, and gnustep-gui (checked on 0.30) draws a bitmap bottom-up even in
	 * a flipped view, ignoring NSImage's flipped flag: so draw through a vertical mirror.
	 * An AppKit that gets this right can run with FLORENCE_NOMIRROR=1. */
	BOOL mirror = getenv("FLORENCE_NOMIRROR") == NULL;
	[NSGraphicsContext saveGraphicsState];
	if (mirror) {
		NSAffineTransform *t = [NSAffineTransform transform];
		[t translateXBy:x0 yBy:y0 + h];
		[t scaleXBy:1.0 yBy:-1.0];
		[t concat];
	}
	[img drawInRect:mirror ? NSMakeRect(0, 0, w, h) : NSMakeRect(x0, y0, w, h)
	       fromRect:NSZeroRect operation:NSCompositeCopy fraction:1.0];
	[NSGraphicsContext restoreGraphicsState];
	[img release];
	[rep release];

	if (hasCaret && NSIntersectsRect(caret, r)) {
		[[NSColor blackColor] set];
		NSRectFill(caret);
	}
}

- (void)invalidatePageRect:(NSRect)r { [self setNeedsDisplayInRect:r]; }

- (void)placeCaret:(NSRect)r
{
	if (hasCaret)
		[self setNeedsDisplayInRect:NSInsetRect(caret, -1, -1)];
	caret = r;
	hasCaret = YES;
	[self setNeedsDisplayInRect:NSInsetRect(caret, -1, -1)];
}

- (void)removeCaret
{
	if (hasCaret)
		[self setNeedsDisplayInRect:NSInsetRect(caret, -1, -1)];
	hasCaret = NO;
}

- (void)setPointer:(int)p
{
	NSCursor *c;
	switch (p) {
	case FLO_PTR_HAND: c = [NSCursor pointingHandCursor]; break;
	case FLO_PTR_IBEAM: c = [NSCursor IBeamCursor]; break;
	case FLO_PTR_CROSS: c = [NSCursor crosshairCursor]; break;
	case FLO_PTR_MOVE: c = [NSCursor openHandCursor]; break;
	default: c = [NSCursor arrowCursor]; break;
	}
	[cursor release];
	cursor = [c retain];
	[c set];
}

/* ---- input --------------------------------------------------------------- */

static int mods_of(NSEvent *e)
{
	NSUInteger f = [e modifierFlags];
	return ((f & NSShiftKeyMask) ? FLO_MOD_SHIFT : 0) | ((f & NSControlKeyMask) ? FLO_MOD_CTRL : 0);
}

- (NSPoint)pageLocation:(NSEvent *)e { return [self convertPoint:[e locationInWindow] fromView:nil]; }

- (void)mouseMoved:(NSEvent *)e
{
	NSPoint p = [self pageLocation:e];
	if (gw != NULL)
		flo_win_mouse(gw, FLO_MOUSE_MOVE, mods_of(e), (int)p.x, (int)p.y);
}

- (void)mouseDown:(NSEvent *)e
{
	NSPoint p = [self pageLocation:e];
	if (gw == NULL)
		return;
	[[self window] makeFirstResponder:self];
	pressed = YES;
	dragging = NO;
	pressPoint = p;
	flo_win_mouse(gw, FLO_MOUSE_PRESS, mods_of(e), (int)p.x, (int)p.y);
}

- (void)mouseDragged:(NSEvent *)e
{
	NSPoint p = [self pageLocation:e];
	if (gw == NULL || !pressed)
		return;
	if (!dragging) {
		if (fabs(p.x - pressPoint.x) < DRAG_SLOP && fabs(p.y - pressPoint.y) < DRAG_SLOP)
			return;
		dragging = YES;
		flo_win_mouse(gw, FLO_MOUSE_DRAG_START, mods_of(e), (int)pressPoint.x, (int)pressPoint.y);
	}
	[self autoscroll:e];
	flo_win_mouse(gw, FLO_MOUSE_DRAG, mods_of(e), (int)p.x, (int)p.y);
}

- (void)mouseUp:(NSEvent *)e
{
	NSPoint p = [self pageLocation:e];
	if (gw == NULL || !pressed)
		return;
	pressed = NO;
	if (dragging) {
		dragging = NO;
		flo_win_mouse(gw, FLO_MOUSE_DRAG_END, mods_of(e), (int)p.x, (int)p.y);
	} else {
		flo_win_mouse(gw, [e clickCount] >= 2 ? FLO_MOUSE_DOUBLE_CLICK : FLO_MOUSE_CLICK,
			      mods_of(e), (int)p.x, (int)p.y);
	}
}

/* the wheel and the scrollers are the NSScrollView's own */

- (void)scrollByY:(CGFloat)dy
{
	NSClipView *clip = (NSClipView *)[self superview];
	NSPoint o = [clip bounds].origin;
	NSScrollView *sv = [self enclosingScrollView];
	NSPoint np = [clip constrainScrollPoint:NSMakePoint(o.x, o.y + dy)];
	[clip scrollToPoint:np];
	[sv reflectScrolledClipView:clip];
}

- (void)keyDown:(NSEvent *)e
{
	NSString *chars = [e characters];
	unsigned key;
	CGFloat page = [[self enclosingScrollView] contentSize].height * 0.9;
	BOOL shift = (mods_of(e) & FLO_MOD_SHIFT) != 0;

	if ([chars length] == 0 || gw == NULL)
		return;
	switch ([chars characterAtIndex:0]) {
	case NSLeftArrowFunctionKey: key = FLO_KEY_LEFT; break;
	case NSRightArrowFunctionKey: key = FLO_KEY_RIGHT; break;
	case NSUpArrowFunctionKey: key = FLO_KEY_UP; break;
	case NSDownArrowFunctionKey: key = FLO_KEY_DOWN; break;
	case NSPageUpFunctionKey: key = FLO_KEY_PAGE_UP; break;
	case NSPageDownFunctionKey: key = FLO_KEY_PAGE_DOWN; break;
	case NSHomeFunctionKey: key = FLO_KEY_HOME; break;
	case NSEndFunctionKey: key = FLO_KEY_END; break;
	case NSDeleteFunctionKey: key = FLO_KEY_DELETE; break;
	case 0x7f: case 8: key = FLO_KEY_BACKSPACE; break;
	case 27: key = FLO_KEY_ESCAPE; break;
	default: {
		unichar c = [chars characterAtIndex:0];
		if (c < 32 && c != '\r' && c != '\t' && c != '\n')
			return;
		if (c >= 0xF700 && c <= 0xF8FF)
			return;                 /* an unmapped function key */
		key = c;
		if (c >= 0xD800 && c < 0xDC00 && [chars length] > 1)
			key = 0x10000 + ((c - 0xD800) << 10) + ([chars characterAtIndex:1] - 0xDC00);
	}
	}
	if (flo_win_key(gw, key, mods_of(e)))
		return;
	/* the page had no use for it: scroll, as a browser does */
	switch (key) {
	case FLO_KEY_DOWN: [self scrollByY:40]; break;
	case FLO_KEY_UP: [self scrollByY:-40]; break;
	case FLO_KEY_PAGE_DOWN: [self scrollByY:page]; break;
	case FLO_KEY_PAGE_UP: [self scrollByY:-page]; break;
	case ' ': [self scrollByY:shift ? -page : page]; break;
	case FLO_KEY_HOME: [self scrollByY:-1e9]; break;
	case FLO_KEY_END: [self scrollByY:1e9]; break;
	}
}

/* Edit menu actions, delivered to the first responder */
- (void)copy:(id)s { if (gw) flo_win_key(gw, FLO_KEY_COPY, 0); }
- (void)cut:(id)s { if (gw) flo_win_key(gw, FLO_KEY_CUT, 0); }
- (void)paste:(id)s { if (gw) flo_win_key(gw, FLO_KEY_PASTE, 0); }
- (void)selectAll:(id)s { if (gw) flo_win_key(gw, FLO_KEY_SELECT_ALL, 0); }

@end
