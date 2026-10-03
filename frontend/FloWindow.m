/* Florence: a browser window. See FloWindow.h. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only */
#import "FloWindow.h"
#include "flo.h"

#define BAR_H 34.0
#define STATUS_H 20.0

NSString *FloURLFromInput(NSString *input)
{
	NSString *s = [input stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];

	if ([s length] == 0)
		return nil;
	if ([s rangeOfString:@"://"].location != NSNotFound || [s hasPrefix:@"about:"] || [s hasPrefix:@"data:"])
		return s;
	/* looks like a host (a dot, no spaces) or localhost: an address; https first, as a modern browser does */
	if ([s rangeOfString:@" "].location == NSNotFound
	    && ([s rangeOfString:@"."].location != NSNotFound || [s hasPrefix:@"localhost"]))
		return [@"https://" stringByAppendingString:s];
	/* anything else: a search, on the engine that needs no JavaScript */
	NSString *q = [s stringByAddingPercentEscapesUsingEncoding:NSUTF8StringEncoding];
	return [@"https://html.duckduckgo.com/html/?q=" stringByAppendingString:q];
}

static NSMutableArray *windows;

/* ---- engine events (C callbacks; `ui` is the FloWindow) -------------------------------------------- */

#define W(ui) ((FloWindow *)(ui))

@interface FloWindow (Events)
- (void)pageTitle:(NSString *)t;
- (void)pageURI:(NSString *)u;
- (void)pageProgress:(double)p;
- (void)pageBack:(BOOL)b forward:(BOOL)f;
- (void)pageHover:(NSString *)l;
- (void)pageChangedX:(int)x y:(int)y w:(int)w h:(int)h;
@end

static NSString *str(const char *s) { return s != NULL ? [NSString stringWithUTF8String:s] : nil; }

static void ev_changed(void *ui, int x, int y, int w, int h) { [W(ui) pageChangedX:x y:y w:w h:h]; }
static void ev_title(void *ui, const char *t) { [W(ui) pageTitle:str(t)]; }
static void ev_uri(void *ui, const char *u) { [W(ui) pageURI:str(u)]; }
static void ev_progress(void *ui, double p) { [W(ui) pageProgress:p]; }
static void ev_nav(void *ui, bool b, bool f) { [W(ui) pageBack:b forward:f]; }
static void ev_hover(void *ui, const char *l) { [W(ui) pageHover:str(l)]; }

static const struct flo_page_events events = { ev_changed, ev_title, ev_uri, ev_progress, ev_nav, ev_hover };

@implementation FloWindow

+ (NSMutableArray *)all
{
	if (windows == nil)
		windows = [[NSMutableArray alloc] init];
	return windows;
}

+ (FloWindow *)key
{
	NSEnumerator *e = [[FloWindow all] objectEnumerator];
	FloWindow *w, *found = nil;

	while ((w = [e nextObject]) != nil)
		if (w->win == [NSApp keyWindow])
			return w;
		else
			found = w;
	return found;
}

static NSButton *button(NSString *title, id target, SEL action, NSRect frame)
{
	NSButton *b = [[[NSButton alloc] initWithFrame:frame] autorelease];
	[b setTitle:title];
	[b setBezelStyle:NSRoundedBezelStyle];
	[b setTarget:target];
	[b setAction:action];
	[b setAutoresizingMask:NSViewMinYMargin];
	return b;
}

- (id)initWithAddress:(NSString *)url
{
	if ((self = [super init]) == nil)
		return nil;
	NSRect frame = NSMakeRect(80, 80, 960, 640);
	win = [[NSWindow alloc] initWithContentRect:frame
		styleMask:NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask | NSResizableWindowMask
		backing:NSBackingStoreBuffered defer:NO];
	[win setTitle:@"Florence"];
	[win setDelegate:self];
	[win setReleasedWhenClosed:NO];
	NSView *content = [win contentView];
	NSRect cb = [content bounds];
	CGFloat top = cb.size.height - BAR_H + 4;

	backBtn = button(@"Back", self, @selector(goBack:), NSMakeRect(6, top, 56, 26));
	fwdBtn = button(@"Fwd", self, @selector(goForward:), NSMakeRect(66, top, 56, 26));
	reloadBtn = button(@"Reload", self, @selector(reloadOrStop:), NSMakeRect(126, top, 64, 26));
	[backBtn setEnabled:NO];
	[fwdBtn setEnabled:NO];
	addr = [[[NSTextField alloc] initWithFrame:NSMakeRect(196, top + 1, cb.size.width - 202, 24)] autorelease];
	[addr setTarget:self];
	[addr setAction:@selector(go:)];                /* Return */
	[addr setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
	status = [[[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, cb.size.width, STATUS_H)] autorelease];
	[status setEditable:NO];
	[status setBordered:NO];
	[status setDrawsBackground:NO];
	[status setFont:[NSFont systemFontOfSize:11]];
	[status setAutoresizingMask:NSViewWidthSizable | NSViewMaxYMargin];

	view = [[[FloPageView alloc] initWithFrame:NSMakeRect(0, STATUS_H, cb.size.width, cb.size.height - BAR_H - STATUS_H)] autorelease];
	[view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

	[content addSubview:view];
	[content addSubview:backBtn];
	[content addSubview:fwdBtn];
	[content addSubview:reloadBtn];
	[content addSubview:addr];
	[content addSubview:status];

	NSSize vs = [view bounds].size;
	page = flo_page_new(&events, self, (int)vs.width, (int)vs.height);
	[view setPage:page];
	[[FloWindow all] addObject:self];
	[win makeKeyAndOrderFront:nil];
	[win makeFirstResponder:url != nil ? (NSResponder *)view : (NSResponder *)addr];
	if (url != nil)
		flo_page_load(page, [url UTF8String]);
	else
		[self focusAddress];
	return self;
}

- (void)dealloc
{
	[win release];
	[super dealloc];
}

- (void)focusAddress { [win makeFirstResponder:addr]; [addr selectText:nil]; }

- (void)go:(id)s
{
	NSString *u = FloURLFromInput([addr stringValue]);
	if (u != nil) {
		flo_page_load(page, [u UTF8String]);
		[win makeFirstResponder:view];
	}
}

- (void)goBack:(id)s { flo_page_back(page); }
- (void)goForward:(id)s { flo_page_forward(page); }
- (void)reloadOrStop:(id)s
{
	if (flo_page_loading(page))
		flo_page_stop(page);
	else
		flo_page_reload(page);
}

/* ---- events from the engine ------------------------------------------------------------------------ */

- (void)pageChangedX:(int)x y:(int)y w:(int)w h:(int)h { [view frameChangedX:x y:y w:w h:h]; }
- (void)pageTitle:(NSString *)t { [win setTitle:[t length] > 0 ? t : @"Florence"]; }

- (void)pageURI:(NSString *)u
{
	/* do not overwrite what the user is typing */
	if ([win firstResponder] != [addr currentEditor] && u != nil)
		[addr setStringValue:u];
}

- (void)pageProgress:(double)p
{
	[reloadBtn setTitle:p < 1.0 && flo_page_loading(page) ? @"Stop" : @"Reload"];
	[status setStringValue:p < 1.0 && flo_page_loading(page) ? [NSString stringWithFormat:@"Loading %d%%", (int)(p * 100)] : @""];
}

- (void)pageBack:(BOOL)b forward:(BOOL)f { [backBtn setEnabled:b]; [fwdBtn setEnabled:f]; }
- (void)pageHover:(NSString *)l { [status setStringValue:l != nil ? l : @""]; }

/* ---- window delegate --------------------------------------------------------------------------------- */

- (void)windowWillClose:(NSNotification *)n
{
	[view setPage:NULL];
	flo_page_free(page);
	page = NULL;
	[win setDelegate:nil];
	[[self retain] autorelease];                    /* we are inside the window's own notification: outlive it */
	[[FloWindow all] removeObject:self];            /* drops the list's reference */
}

@end
