/* Florence: the page view. See FloPageView.h. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT */
#import "FloPageView.h"
#include <math.h>
#include <stdlib.h>
#include "flo.h"

@implementation FloPageView

- (id)initWithFrame:(NSRect)f
{
	if ((self = [super initWithFrame:f]) != nil)
		sentSize = f.size;
	return self;
}

- (void)dealloc
{
	free(rgb);
	[super dealloc];
}

- (void)setPage:(struct flo_page *)p { page = p; }
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }

- (BOOL)becomeFirstResponder
{
	if (page != NULL)
		flo_page_focus(page, true);
	return YES;
}

- (BOOL)resignFirstResponder
{
	if (page != NULL)
		flo_page_focus(page, false);
	return YES;
}

- (void)viewDidMoveToWindow { [[self window] setAcceptsMouseMovedEvents:YES]; }

/* ---- size: tell the engine once the user stops dragging the window edge -------------------------- */

- (void)sendSize
{
	NSSize s = [self bounds].size;
	if (page != NULL && (s.width != sentSize.width || s.height != sentSize.height) && s.width > 0 && s.height > 0) {
		sentSize = s;
		flo_page_resize(page, (int)s.width, (int)s.height);
	}
}

- (void)setFrameSize:(NSSize)s
{
	[super setFrameSize:s];
	[NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(sendSize) object:nil];
	[self performSelector:@selector(sendSize) withObject:nil afterDelay:0.08];     /* one reflow per drag */
}

/* ---- drawing -------------------------------------------------------------------------------------- */

- (void)frameChangedX:(int)x y:(int)y w:(int)w h:(int)h
{
	[self setNeedsDisplayInRect:NSMakeRect(x, y, w, h)];
}

- (void)drawRect:(NSRect)dirty
{
	int fw = 0, fh = 0, stride = 0, y;
	const unsigned char *px = page != NULL ? flo_page_pixels(page, &fw, &fh, &stride) : NULL;
	NSRect r = NSIntersectionRect(dirty, NSMakeRect(0, 0, fw, fh));
	int x0, y0, w, h;

	[[NSColor whiteColor] set];
	if (px == NULL || NSIsEmptyRect(r)) {
		NSRectFill(dirty);      /* before the first frame, or outside the frame the engine last drew (mid-resize) */
		return;
	}
	if (NSMaxX(dirty) > fw || NSMaxY(dirty) > fh)
		NSRectFill(dirty);
	x0 = (int)floor(NSMinX(r)); y0 = (int)floor(NSMinY(r));
	w = (int)ceil(NSMaxX(r)) - x0; h = (int)ceil(NSMaxY(r)) - y0;
	if ((size_t)w * h * 3 > rgbCap) {
		unsigned char *n = realloc(rgb, (size_t)w * h * 3);
		if (n == NULL)
			return;
		rgb = n;
		rgbCap = (size_t)w * h * 3;
	}
	/* BGRA premultiplied (the page is opaque) to the R,G,B AppKit wants */
	for (y = 0; y < h; y++) {
		const unsigned char *s = px + (size_t)(y0 + y) * stride + (size_t)x0 * 4;
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
	/* gnustep-gui (0.30) draws a bitmap bottom-up even in a flipped view, so draw through a vertical
	 * mirror; an AppKit that gets this right can run with FLORENCE_NOMIRROR=1. */
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
}

/* ---- input ----------------------------------------------------------------------------------------- */

static int mods_of(NSEvent *e)
{
	NSUInteger f = [e modifierFlags];
	return ((f & NSShiftKeyMask) ? FLO_MOD_SHIFT : 0) | ((f & NSControlKeyMask) ? FLO_MOD_CTRL : 0)
	     | ((f & NSAlternateKeyMask) ? FLO_MOD_ALT : 0) | ((f & NSCommandKeyMask) ? FLO_MOD_META : 0);
}

- (NSPoint)at:(NSEvent *)e { return [self convertPoint:[e locationInWindow] fromView:nil]; }

- (void)mouseMoved:(NSEvent *)e
{
	NSPoint p = [self at:e];
	if (page != NULL)
		flo_page_pointer_move(page, mods_of(e), p.x, p.y);
}

- (void)mouseDragged:(NSEvent *)e { [self mouseMoved:e]; }
- (void)rightMouseDragged:(NSEvent *)e { [self mouseMoved:e]; }
- (void)otherMouseDragged:(NSEvent *)e { [self mouseMoved:e]; }

- (void)button:(int)b down:(BOOL)down event:(NSEvent *)e
{
	NSPoint p = [self at:e];
	if (page != NULL)
		flo_page_pointer_button(page, mods_of(e), b, down, (int)[e clickCount], p.x, p.y);
}

- (void)mouseDown:(NSEvent *)e { [[self window] makeFirstResponder:self]; [self button:1 down:YES event:e]; }
- (void)mouseUp:(NSEvent *)e { [self button:1 down:NO event:e]; }
- (void)otherMouseDown:(NSEvent *)e { [self button:2 down:YES event:e]; }
- (void)otherMouseUp:(NSEvent *)e { [self button:2 down:NO event:e]; }
- (void)rightMouseDown:(NSEvent *)e { [self button:3 down:YES event:e]; }
- (void)rightMouseUp:(NSEvent *)e { [self button:3 down:NO event:e]; }

- (void)scrollWheel:(NSEvent *)e
{
	NSPoint p = [self at:e];
	/* Same convention on both sides: AppKit's (gnustep-back sends deltaY +1 for wheel up, XGServerEvent.m:506) and WebCore's
	 * wheel deltas are positive for "scroll up"/"scroll left", which is what WPE passes through unchanged (found on the Pi:
	 * +5 at the top of a page is "up" and, correctly, does nothing). No sign change. */
	if (page != NULL)
		flo_page_scroll(page, mods_of(e), [e deltaX], [e deltaY], p.x, p.y);
}

static int special_of(unichar c)
{
	switch (c) {
	case NSUpArrowFunctionKey: return FLO_KEY_UP;
	case NSDownArrowFunctionKey: return FLO_KEY_DOWN;
	case NSLeftArrowFunctionKey: return FLO_KEY_LEFT;
	case NSRightArrowFunctionKey: return FLO_KEY_RIGHT;
	case NSHomeFunctionKey: return FLO_KEY_HOME;
	case NSEndFunctionKey: return FLO_KEY_END;
	case NSPageUpFunctionKey: return FLO_KEY_PAGE_UP;
	case NSPageDownFunctionKey: return FLO_KEY_PAGE_DOWN;
	case NSDeleteFunctionKey: return FLO_KEY_DELETE;
	case NSBackspaceCharacter: case 0x7f: return FLO_KEY_BACKSPACE;
	case NSCarriageReturnCharacter: case NSNewlineCharacter: case NSEnterCharacter: return FLO_KEY_ENTER;
	case NSTabCharacter: case NSBackTabCharacter: return FLO_KEY_TAB;
	case 0x1b: return FLO_KEY_ESCAPE;
	default: return 0;
	}
}

- (void)key:(NSEvent *)e down:(BOOL)down
{
	NSString *s = [e characters];
	NSUInteger i;

	if (page == NULL || [s length] == 0)
		return;
	for (i = 0; i < [s length]; i++) {
		unichar c = [s characterAtIndex:i];
		int sk = special_of(c);
		if (sk != 0) {
			flo_page_special_key(page, mods_of(e), sk, down);
		} else if (c >= 0x20 && !(c >= 0xf700 && c <= 0xf8ff)) {      /* printable, not an AppKit function-key code */
			uint32_t cp = c;
			if (c >= 0xd800 && c < 0xdc00 && i + 1 < [s length]) {      /* a surrogate pair */
				unichar lo = [s characterAtIndex:++i];
				cp = 0x10000 + (((uint32_t)c - 0xd800) << 10) + (lo - 0xdc00);
			}
			flo_page_key(page, mods_of(e), cp, down);
		}
	}
}

- (void)keyDown:(NSEvent *)e { [self key:e down:YES]; }
- (void)keyUp:(NSEvent *)e { [self key:e down:NO]; }

@end
