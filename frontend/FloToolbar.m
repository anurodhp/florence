/* Florence: Safari-like toolbar parts. See FloToolbar.h. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only */
#import "FloToolbar.h"
#import "FloBrowser.h"
#include <math.h>

static NSColor *grey(CGFloat w) { return [NSColor colorWithCalibratedWhite:w alpha:1.0]; }

/* ---- icons: drawn in a 20x20 box centred on `c`, stroked in `colour` ------------ */

static void strokeLine(NSPoint a, NSPoint b)
{
	NSBezierPath *p = [NSBezierPath bezierPath];
	[p moveToPoint:a];
	[p lineToPoint:b];
	[p stroke];
}

static void drawIcon(FloIcon icon, NSPoint c, NSColor *colour)
{
	NSBezierPath *p;
	NSInteger i;

	[colour set];
	[NSBezierPath setDefaultLineWidth:1.8];
	[NSBezierPath setDefaultLineCapStyle:NSRoundLineCapStyle];
	[NSBezierPath setDefaultLineJoinStyle:NSRoundLineJoinStyle];
	switch (icon) {
	case FloIconBack:
	case FloIconForward: {
		CGFloat s = icon == FloIconBack ? -1 : 1;
		p = [NSBezierPath bezierPath];
		[p moveToPoint:NSMakePoint(c.x - 2.5 * s, c.y + 6.5)];
		[p lineToPoint:NSMakePoint(c.x + 2.5 * s, c.y)];
		[p lineToPoint:NSMakePoint(c.x - 2.5 * s, c.y - 6.5)];
		[p setLineWidth:2.2];
		[p stroke];
		break;
	}
	case FloIconReload: {
		CGFloat r = 5.6, end = 335, ex, ey, tx, ty;
		p = [NSBezierPath bezierPath];
		[p appendBezierPathWithArcWithCenter:c radius:r startAngle:35 endAngle:end];
		[p setLineWidth:1.7];
		[p stroke];
		/* arrow head at the end of the arc, pointing along it */
		ex = c.x + r * cos(end * M_PI / 180);
		ey = c.y + r * sin(end * M_PI / 180);
		tx = -sin(end * M_PI / 180);
		ty = cos(end * M_PI / 180);
		p = [NSBezierPath bezierPath];
		[p moveToPoint:NSMakePoint(ex + tx * 3.6, ey + ty * 3.6)];
		[p lineToPoint:NSMakePoint(ex - ty * 3.2, ey + tx * 3.2)];
		[p lineToPoint:NSMakePoint(ex + ty * 3.2, ey - tx * 3.2)];
		[p closePath];
		[p fill];
		break;
	}
	case FloIconStop:
		strokeLine(NSMakePoint(c.x - 4.5, c.y - 4.5), NSMakePoint(c.x + 4.5, c.y + 4.5));
		strokeLine(NSMakePoint(c.x - 4.5, c.y + 4.5), NSMakePoint(c.x + 4.5, c.y - 4.5));
		break;
	case FloIconPlus:
		strokeLine(NSMakePoint(c.x - 6, c.y), NSMakePoint(c.x + 6, c.y));
		strokeLine(NSMakePoint(c.x, c.y - 6), NSMakePoint(c.x, c.y + 6));
		break;
	case FloIconStar:
	case FloIconStarFilled:
		p = [NSBezierPath bezierPath];
		for (i = 0; i < 10; i++) {
			CGFloat a = M_PI / 2 + i * M_PI / 5, rad = (i % 2 == 0) ? 7.6 : 3.3;
			NSPoint q = NSMakePoint(c.x + rad * cos(a), c.y + rad * sin(a) - 0.4);
			if (i == 0)
				[p moveToPoint:q];
			else
				[p lineToPoint:q];
		}
		[p closePath];
		[p setLineWidth:1.5];
		if (icon == FloIconStarFilled) {
			[[NSColor colorWithCalibratedRed:0.96 green:0.62 blue:0.10 alpha:1.0] set];
			[p fill];
		}
		[p stroke];
		break;
	}
}

/* ---- FloToolButton --------------------------------------------------------------- */

@implementation FloToolButton

- (id)initWithFrame:(NSRect)f
{
	if ((self = [super initWithFrame:f]) != nil) {
		[self setBordered:NO];
		[self setButtonType:NSMomentaryChangeButton];
		[self setTitle:@""];
	}
	return self;
}

- (void)setIcon:(FloIcon)i
{
	icon = i;
	[self setNeedsDisplay:YES];
}

- (void)setEnabled:(BOOL)e
{
	[super setEnabled:e];
	[self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirty
{
	NSRect b = [self bounds];
	BOOL down = [[self cell] isHighlighted], en = [self isEnabled];

	if (en && (hover || down)) {
		[[NSColor colorWithCalibratedWhite:0 alpha:down ? 0.16 : 0.08] set];
		[[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(b, 1, 1) xRadius:5 yRadius:5] fill];
	}
	drawIcon(icon, NSMakePoint(NSMidX(b), NSMidY(b)), en ? grey(down ? 0.08 : 0.30) : grey(0.72));
}

/* hover tracking */
- (void)resetTracking
{
	if (tracking)
		[self removeTrackingRect:track];
	tracking = NO;
	if ([self window] != nil && !NSIsEmptyRect([self bounds])) {
		track = [self addTrackingRect:[self bounds] owner:self userData:NULL assumeInside:NO];
		tracking = YES;
	}
}
- (void)viewDidMoveToWindow { [self resetTracking]; }
- (void)setFrame:(NSRect)f { [super setFrame:f]; [self resetTracking]; }
- (void)mouseEntered:(NSEvent *)e { hover = YES; [self setNeedsDisplay:YES]; }
- (void)mouseExited:(NSEvent *)e { hover = NO; [self setNeedsDisplay:YES]; }

- (void)dealloc
{
	if (tracking && [self window] != nil)
		[self removeTrackingRect:track];
	[super dealloc];
}

@end

/* the address text field: its editor draws no background of its own, and the bar repaints its
 * focus ring when focus arrives */
@interface FloAddressField : NSTextField
@end

@implementation FloAddressField

- (BOOL)becomeFirstResponder
{
	BOOL r = [super becomeFirstResponder];
	/* white behind the text while editing (the focused bar is white): a transparent editor
	 * paints its selection across the whole field */
	[self setDrawsBackground:YES];
	[self setBackgroundColor:[NSColor whiteColor]];
	[[self superview] setNeedsDisplay:YES];
	return r;
}

- (void)textDidEndEditing:(NSNotification *)n
{
	[super textDidEndEditing:n];
	[self setDrawsBackground:NO];
	[[self superview] setNeedsDisplay:YES];
}

@end

/* ---- FloAddressBar --------------------------------------------------------------- */

@implementation FloAddressBar

- (id)initWithTarget:(id)target goAction:(SEL)go reloadAction:(SEL)reloadAction
{
	if ((self = [super initWithFrame:NSMakeRect(0, 0, 300, 26)]) == nil)
		return nil;
	field = [[FloAddressField alloc] initWithFrame:NSMakeRect(10, 4, 250, 18)];
	[field setBordered:NO];
	[field setBezeled:NO];
	[field setDrawsBackground:NO];
	[field setFont:[NSFont systemFontOfSize:13]];
	[[field cell] setWraps:NO];
	[[field cell] setScrollable:YES];               /* one line that scrolls, as an address field does */
	[field setTarget:target];
	[field setAction:go];
	if ([field respondsToSelector:@selector(setFocusRingType:)])
		[field setFocusRingType:NSFocusRingTypeNone];
	[self addSubview:field];
	reload = [[FloToolButton alloc] initWithFrame:NSMakeRect(0, 2, 22, 22)];
	[reload setIcon:FloIconReload];
	[reload setTarget:target];
	[reload setAction:reloadAction];
	[self addSubview:reload];
	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(editingChanged:)
		name:NSControlTextDidBeginEditingNotification object:field];
	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(editingChanged:)
		name:NSControlTextDidEndEditingNotification object:field];
	return self;
}

- (void)dealloc
{
	[[NSNotificationCenter defaultCenter] removeObserver:self];
	[field release];
	[reload release];
	[super dealloc];
}

- (NSTextField *)field { return field; }
- (FloToolButton *)reloadButton { return reload; }
- (void)editingChanged:(NSNotification *)n { [self setNeedsDisplay:YES]; }

- (void)setSecure:(BOOL)s
{
	if (s != secure) {
		secure = s;
		[self layoutInside];
		[self setNeedsDisplay:YES];
	}
}

- (void)layoutInside
{
	NSRect b = [self bounds];
	CGFloat x = secure ? 26 : 10;
	[field setFrame:NSMakeRect(x, (b.size.height - 18) / 2, b.size.width - x - 30, 18)];
	[reload setFrame:NSMakeRect(b.size.width - 26, (b.size.height - 22) / 2, 22, 22)];
}

- (void)setFrame:(NSRect)f { [super setFrame:f]; [self layoutInside]; }

- (void)drawRect:(NSRect)dirty
{
	NSRect b = NSInsetRect([self bounds], 0.5, 0.5);
	BOOL focus = [field currentEditor] != nil;
	NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect:b xRadius:6 yRadius:6];

	[(focus ? grey(1.0) : grey(0.925)) set];
	[p fill];
	[(focus ? [NSColor colorWithCalibratedRed:0.30 green:0.58 blue:0.96 alpha:1.0] : grey(0.80)) set];
	[p setLineWidth:focus ? 2.0 : 1.0];
	[p stroke];
	if (secure) {                   /* a small padlock */
		NSRect body = NSMakeRect(9, NSMidY(b) - 5, 9, 7);
		NSBezierPath *sh = [NSBezierPath bezierPath];
		[grey(0.40) set];
		[sh appendBezierPathWithArcWithCenter:NSMakePoint(NSMidX(body), NSMaxY(body)) radius:2.8 startAngle:0 endAngle:180];
		[sh setLineWidth:1.5];
		[sh stroke];
		[[NSBezierPath bezierPathWithRoundedRect:body xRadius:1.5 yRadius:1.5] fill];
	}
}

@end

/* ---- FloToolbarBand -------------------------------------------------------------- */

@implementation FloToolbarBand

- (BOOL)isOpaque { return YES; }

- (void)drawRect:(NSRect)dirty
{
	NSRect b = [self bounds];
	[grey(0.915) set];
	NSRectFill(b);
	[grey(0.72) set];
	NSRectFill(NSMakeRect(0, 0, b.size.width, 1));
}

@end

/* ---- FloTabStrip ------------------------------------------------------------------ */

#define TAB_MAXW 240.0

@implementation FloTabStrip

- (void)setBrowser:(FloBrowser *)b { browser = b; }
- (BOOL)isOpaque { return YES; }

- (CGFloat)tabWidth
{
	NSUInteger n = [browser->tabs count];
	CGFloat w = n > 0 ? [self bounds].size.width / n : 0;
	return w > TAB_MAXW ? TAB_MAXW : floor(w);
}

static NSString *fitted(NSString *s, NSDictionary *attrs, CGFloat width)
{
	NSString *t = s;
	if ([t sizeWithAttributes:attrs].width <= width)
		return t;
	while ([t length] > 1) {
		t = [t substringToIndex:[t length] - 1];
		if ([[t stringByAppendingString:@"..."] sizeWithAttributes:attrs].width <= width)
			break;
	}
	return [t stringByAppendingString:@"..."];
}

- (void)drawRect:(NSRect)dirty
{
	NSRect b = [self bounds];
	CGFloat tw = [self tabWidth];
	NSUInteger i;
	NSDictionary *attrs = [NSDictionary dictionaryWithObjectsAndKeys:
		[NSFont systemFontOfSize:11], NSFontAttributeName, grey(0.18), NSForegroundColorAttributeName, nil];

	[grey(0.80) set];
	NSRectFill(b);
	for (i = 0; i < [browser->tabs count]; i++) {
		FloTab *t = [browser->tabs objectAtIndex:i];
		NSRect r = NSMakeRect(i * tw, 0, tw, b.size.height);
		NSString *title = fitted([t displayTitle], attrs, tw - 54);
		NSSize ts = [title sizeWithAttributes:attrs];
		NSPoint c;

		[(t == browser->current ? grey(0.965) : grey(0.86)) set];
		NSRectFill(r);
		[grey(0.70) set];
		NSRectFill(NSMakeRect(NSMaxX(r) - 1, 0, 1, r.size.height));          /* separator */
		NSRectFill(NSMakeRect(NSMinX(r), 0, r.size.width, 1));                /* hairline below */
		[title drawAtPoint:NSMakePoint(NSMidX(r) - ts.width / 2, NSMidY(r) - ts.height / 2) withAttributes:attrs];
		/* the close mark */
		c = NSMakePoint(NSMinX(r) + 14, NSMidY(r));
		[grey(0.40) set];
		[NSBezierPath setDefaultLineWidth:1.4];
		[NSBezierPath setDefaultLineCapStyle:NSRoundLineCapStyle];
		strokeLine(NSMakePoint(c.x - 3, c.y - 3), NSMakePoint(c.x + 3, c.y + 3));
		strokeLine(NSMakePoint(c.x - 3, c.y + 3), NSMakePoint(c.x + 3, c.y - 3));
	}
}

- (void)mouseDown:(NSEvent *)e
{
	NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
	CGFloat tw = [self tabWidth];
	NSUInteger i;

	if (tw <= 0)
		return;
	i = (NSUInteger)(p.x / tw);
	if (i >= [browser->tabs count])
		return;
	FloTab *t = [browser->tabs objectAtIndex:i];
	if (p.x - i * tw < 26) {        /* the close mark */
		if (t->gw != NULL)
			flo_win_close(t->gw);
	} else {
		[browser selectTab:t];
	}
}

@end

/* ---- FloStatusLabel --------------------------------------------------------------- */

@implementation FloStatusLabel

- (void)dealloc { [text release]; [super dealloc]; }

- (NSDictionary *)attrs
{
	return [NSDictionary dictionaryWithObjectsAndKeys:
		[NSFont systemFontOfSize:11], NSFontAttributeName, grey(0.15), NSForegroundColorAttributeName, nil];
}

- (void)setText:(NSString *)t
{
	NSRect sup = [[self superview] bounds];
	CGFloat w;
	if (t == nil || [t length] == 0) {
		[self setHidden:YES];
		return;
	}
	[text release];
	text = [t copy];
	w = [text sizeWithAttributes:[self attrs]].width + 20;
	if (w > sup.size.width * 0.7)
		w = sup.size.width * 0.7;
	[self setFrame:NSMakeRect(0, 0, w, 20)];
	[self setHidden:NO];
	[self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirty
{
	NSRect b = [self bounds];
	NSDictionary *a = [self attrs];
	NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(-8, -8, b.size.width + 8, b.size.height + 8)
		xRadius:8 yRadius:8];

	[[NSColor colorWithCalibratedWhite:0.97 alpha:1.0] set];
	[p fill];
	[grey(0.74) set];
	[p setLineWidth:1.0];
	[p stroke];
	if (text != nil) {
		NSString *t = text;
		if ([t sizeWithAttributes:a].width > b.size.width - 20) {
			while ([t length] > 1 && [[t stringByAppendingString:@"..."] sizeWithAttributes:a].width > b.size.width - 20)
				t = [t substringToIndex:[t length] - 1];
			t = [t stringByAppendingString:@"..."];
		}
		[t drawAtPoint:NSMakePoint(10, (b.size.height - [t sizeWithAttributes:a].height) / 2) withAttributes:a];
	}
}

@end
