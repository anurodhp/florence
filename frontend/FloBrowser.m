/* Florence: a browser window with tabs. See FloBrowser.h. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only */
#import "FloBrowser.h"
#include <math.h>

#define BAR_H 30.0
#define TAB_H 24.0
#define STATUS_H 18.0
#define TAB_MAX_W 190.0

static NSMutableArray *allBrowsers;

NSString *FloURLFromInput(NSString *in)
{
	NSString *s = [in stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
	if ([s length] == 0)
		return nil;
	if ([s rangeOfString:@"://"].location != NSNotFound || [s hasPrefix:@"about:"] ||
	    [s hasPrefix:@"file:"] || [s hasPrefix:@"data:"])
		return s;
	if ([s rangeOfString:@" "].location == NSNotFound &&
	    ([s rangeOfString:@"."].location != NSNotFound || [s hasPrefix:@"localhost"]))
		return [@"http://" stringByAppendingString:s];
	/* not an address: search (DuckDuckGo's HTML page is the light one) */
	NSMutableCharacterSet *ok = [[[NSCharacterSet alphanumericCharacterSet] mutableCopy] autorelease];
	[ok addCharactersInString:@"-._~"];
	return [@"https://html.duckduckgo.com/html/?q=" stringByAppendingString:
		[s stringByAddingPercentEncodingWithAllowedCharacters:ok]];
}

@implementation FloBrowser

+ (NSMutableArray *)all
{
	if (allBrowsers == nil)
		allBrowsers = [[NSMutableArray alloc] init];
	return allBrowsers;
}

+ (FloBrowser *)key
{
	id d = [[NSApp keyWindow] delegate];
	return [d isKindOfClass:[FloBrowser class]] ? d : nil;
}

static NSButton *makeButton(NSString *title, id target, SEL action, NSView *in)
{
	NSButton *b = [[[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 50, BAR_H - 6)] autorelease];
	[b setTitle:title];
	[b setTarget:target];
	[b setAction:action];
	[b setBezelStyle:NSRoundedBezelStyle];
	[in addSubview:b];
	return b;
}

- (id)init
{
	if ((self = [super init]) == nil)
		return nil;
	tabs = [[NSMutableArray alloc] init];
	CGFloat n = (CGFloat)[[FloBrowser all] count];
	NSRect frame = NSMakeRect(60 + 24 * fmod(n, 8), 60 + 24 * fmod(n, 8), 960, 640);
	win = [[NSWindow alloc] initWithContentRect:frame
		styleMask:NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask | NSResizableWindowMask
		backing:NSBackingStoreBuffered defer:NO];
	[win setReleasedWhenClosed:NO];
	[win setDelegate:self];
	[win setTitle:@"Florence"];
	[win setAcceptsMouseMovedEvents:YES];
	[win setMinSize:NSMakeSize(360, 220)];
	if (FloAppIcon() != nil)
		[win setMiniwindowImage:FloAppIcon()];

	NSView *cv = [win contentView];
	backBtn = makeButton(@"Back", self, @selector(goBack:), cv);
	fwdBtn = makeButton(@"Fwd", self, @selector(goForward:), cv);
	reloadBtn = makeButton(@"Reload", self, @selector(reloadOrStop:), cv);
	newTabBtn = makeButton(@"+", self, @selector(newTabAction:), cv);
	urlField = [[[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 100, BAR_H - 8)] autorelease];
	[urlField setTarget:self];
	[urlField setAction:@selector(go:)];
	[cv addSubview:urlField];

	strip = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 100, TAB_H)] autorelease];
	[strip setHidden:YES];
	[cv addSubview:strip];

	container = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)] autorelease];
	[cv addSubview:container];

	status = [[[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 100, STATUS_H)] autorelease];
	[status setEditable:NO];
	[status setSelectable:NO];
	[status setBezeled:NO];
	[status setDrawsBackground:NO];
	[status setFont:[NSFont systemFontOfSize:11]];
	[cv addSubview:status];

	[[FloBrowser all] addObject:self];
	[self relayout];
	/* Not shown yet: showing it runs delegate callbacks that ask the core about history, and the
	 * core has not finished building this browser window while it is calling us. Next loop pass. */
	[self performSelector:@selector(showWindow) withObject:nil afterDelay:0];
	return self;
}

- (void)dealloc
{
	[NSObject cancelPreviousPerformRequestsWithTarget:self];
	[tabs release];
	[win release];
	[super dealloc];
}

- (void)showWindow
{
	flo_trace("ui: show window");
	[win makeKeyAndOrderFront:nil];
	if (current != nil)
		[win makeFirstResponder:current->page];
	[self refreshChrome];
}

/* ---- layout --------------------------------------------------------------- */

- (void)relayout
{
	NSRect b = [[win contentView] bounds];
	CGFloat W = b.size.width, H = b.size.height, top = H - BAR_H;
	CGFloat stripH = [tabs count] > 1 ? TAB_H : 0;

	[backBtn setFrame:NSMakeRect(4, top + 3, 52, BAR_H - 6)];
	[fwdBtn setFrame:NSMakeRect(58, top + 3, 46, BAR_H - 6)];
	[reloadBtn setFrame:NSMakeRect(106, top + 3, 64, BAR_H - 6)];
	[newTabBtn setFrame:NSMakeRect(W - 34, top + 3, 30, BAR_H - 6)];
	[urlField setFrame:NSMakeRect(176, top + 4, W - 176 - 40, BAR_H - 8)];
	[strip setHidden:stripH == 0];
	[strip setFrame:NSMakeRect(0, top - stripH, W, stripH)];
	[status setFrame:NSMakeRect(0, 0, W, STATUS_H)];
	[container setFrame:NSMakeRect(0, STATUS_H, W, top - stripH - STATUS_H)];
	if (current != nil)
		[current->scroll setFrame:[container bounds]];
	[self layoutStrip];
}

- (void)windowDidResize:(NSNotification *)n { [self relayout]; }

/* the tab strip: a button per tab and a small close button beside it */
- (void)rebuildStrip
{
	NSArray *old = [[strip subviews] copy];
	NSUInteger i;
	[old makeObjectsPerformSelector:@selector(removeFromSuperview)];
	[old release];
	if ([tabs count] < 2)
		return;
	for (i = 0; i < [tabs count]; i++) {
		FloTab *t = [tabs objectAtIndex:i];
		NSButton *tb = [[[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 100, TAB_H - 2)] autorelease];
		NSButton *cb = [[[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 18, TAB_H - 2)] autorelease];
		[tb setTitle:[t displayTitle]];
		[tb setTag:(NSInteger)i];
		[tb setTarget:self];
		[tb setAction:@selector(tabButton:)];
		[tb setButtonType:NSPushOnPushOffButton];
		[tb setBezelStyle:NSShadowlessSquareBezelStyle];
		[tb setFont:[NSFont systemFontOfSize:11]];
		[tb setState:t == current ? NSOnState : NSOffState];
		[[tb cell] setLineBreakMode:NSLineBreakByTruncatingTail];
		[cb setTitle:@"x"];
		[cb setTag:(NSInteger)i];
		[cb setTarget:self];
		[cb setAction:@selector(closeButton:)];
		[cb setBezelStyle:NSShadowlessSquareBezelStyle];
		[cb setFont:[NSFont systemFontOfSize:10]];
		[strip addSubview:tb];
		[strip addSubview:cb];
	}
	[self layoutStrip];
}

- (void)layoutStrip
{
	NSArray *v = [strip subviews];
	NSUInteger n = [v count] / 2, i;
	CGFloat avail = [strip bounds].size.width - 8;
	CGFloat tw = n > 0 ? floor(avail / n) : 0;
	if (tw > TAB_MAX_W)
		tw = TAB_MAX_W;
	for (i = 0; i < n; i++) {
		CGFloat x = 4 + i * tw;
		[[v objectAtIndex:2 * i] setFrame:NSMakeRect(x, 1, tw - 18, TAB_H - 2)];
		[[v objectAtIndex:2 * i + 1] setFrame:NSMakeRect(x + tw - 18, 1, 18, TAB_H - 2)];
	}
}

- (void)tabButton:(id)s { [self selectTab:[tabs objectAtIndex:(NSUInteger)[s tag]]]; }

- (void)closeButton:(id)s
{
	FloTab *t = [tabs objectAtIndex:(NSUInteger)[s tag]];
	if (t->gw != NULL)
		flo_win_close(t->gw);
}

/* ---- tabs ------------------------------------------------------------------- */

- (void)addTab:(FloTab *)t select:(BOOL)select
{
	t->browser = self;
	[tabs addObject:t];
	[container addSubview:t->scroll];
	[t->scroll setFrame:[container bounds]];       /* a valid size before the core first asks for it */
	[t->scroll setHidden:YES];
	if (current == nil || select)
		[self selectTab:t];
	else
		[self relayout];
	[self rebuildStrip];
	[self setNeedsChrome];
}

- (void)selectTab:(FloTab *)t
{
	if (t == nil || ![tabs containsObject:t])
		return;
	if (current != nil && current != t)
		[current->scroll setHidden:YES];
	current = t;
	[t->scroll setFrame:[container bounds]];
	[t->scroll setHidden:NO];
	[t applyResize];
	[win makeFirstResponder:t->page];
	[self relayout];
	[self rebuildStrip];
	[self setNeedsChrome];
}

- (void)removeTab:(FloTab *)t
{
	NSUInteger i = [tabs indexOfObject:t];
	if (i == NSNotFound)
		return;
	[t retain];
	[tabs removeObjectAtIndex:i];
	[t->scroll removeFromSuperview];
	t->browser = nil;
	if (current == t) {
		current = nil;
		if ([tabs count] > 0)
			[self selectTab:[tabs objectAtIndex:i < [tabs count] ? i : [tabs count] - 1]];
	}
	[t release];
	if ([tabs count] == 0) {
		if (!closing) {
			closing = YES;
			[win setDelegate:nil];
			[win close];
		}
		[[self retain] autorelease];
		[[FloBrowser all] removeObject:self];
		if ([[FloBrowser all] count] == 0)
			[NSApp terminate:nil];
		return;
	}
	[self relayout];
	[self rebuildStrip];
	[self setNeedsChrome];
}

- (void)closeCurrentTab
{
	if (current != nil && current->gw != NULL)
		flo_win_close(current->gw);
}

- (void)nextTab:(int)delta
{
	NSUInteger n = [tabs count], i;
	if (n < 2 || current == nil)
		return;
	i = [tabs indexOfObject:current];
	[self selectTab:[tabs objectAtIndex:(i + n + delta) % n]];
}

- (void)newTab
{
	if (current != nil && current->gw != NULL)
		flo_win_new_tab(current->gw, NULL);
}

- (void)newTabAction:(id)s { [self newTab]; }

/* the window's red close button: close every tab */
- (void)windowWillClose:(NSNotification *)n
{
	NSArray *all;
	NSUInteger i;
	if (closing)
		return;
	closing = YES;
	[[self retain] autorelease];
	all = [[tabs copy] autorelease];
	for (i = 0; i < [all count]; i++) {
		FloTab *t = [all objectAtIndex:i];
		if (t->gw != NULL)
			flo_win_close(t->gw);   /* the core calls back flo_ui_window_free -> removeTab */
	}
	[[FloBrowser all] removeObject:self];
	if ([[FloBrowser all] count] == 0)
		[NSApp terminate:nil];
}

- (void)windowDidBecomeKey:(NSNotification *)n { [self setNeedsChrome]; }

/* ---- chrome ------------------------------------------------------------------ */

- (void)setNeedsChrome
{
	if (chromePending)
		return;
	chromePending = YES;
	[self performSelector:@selector(refreshChrome) withObject:nil afterDelay:0];
}

- (void)refreshChrome
{
	chromePending = NO;
	if (current == nil || current->gw == NULL || closing)
		return;
	[win setTitle:[current displayTitle]];
	if ([urlField currentEditor] == nil)            /* not while the user is typing */
		[urlField setStringValue:current->url];
	[status setStringValue:current->status];
	[backBtn setEnabled:flo_win_can_back(current->gw)];
	[fwdBtn setEnabled:flo_win_can_forward(current->gw)];
	[reloadBtn setTitle:current->loading ? @"Stop" : @"Reload"];
	if ([tabs count] > 1) {
		NSArray *v = [strip subviews];
		NSUInteger i;
		for (i = 0; i < [tabs count] && 2 * i + 1 < [v count]; i++) {
			FloTab *t = [tabs objectAtIndex:i];
			NSButton *tb = [v objectAtIndex:2 * i];
			if (![[tb title] isEqualToString:[t displayTitle]])
				[tb setTitle:[t displayTitle]];
			[tb setState:t == current ? NSOnState : NSOffState];
		}
	}
}

- (void)focusLocation
{
	[win makeFirstResponder:urlField];
	[urlField selectText:nil];
}

- (void)go:(id)sender
{
	NSString *u = FloURLFromInput([urlField stringValue]);
	if (u != nil && current != nil && current->gw != NULL) {
		flo_win_navigate(current->gw, [u UTF8String]);
		[win makeFirstResponder:current->page];
	}
}
- (void)goBack:(id)s { if (current && current->gw) flo_win_back(current->gw); }
- (void)goForward:(id)s { if (current && current->gw) flo_win_forward(current->gw); }
- (void)reloadOrStop:(id)s
{
	if (current == nil || current->gw == NULL)
		return;
	if (current->loading)
		flo_win_stop(current->gw);
	else
		flo_win_reload(current->gw);
}

@end
