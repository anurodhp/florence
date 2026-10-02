/* Florence: a browser window with tabs. See FloBrowser.h. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only */
#import "FloBrowser.h"
#include <math.h>
#import "FloStore.h"

NSString *const FloBookmarksChanged = @"FloBookmarksChanged";

#define BAR_H 38.0
#define BTN_W 28.0
#define BTN_H 26.0
#define TAB_H 26.0
#define FIND_H 34.0

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

static FloToolButton *makeButton(FloIcon icon, id target, SEL action, NSView *in)
{
	FloToolButton *b = [[[FloToolButton alloc] initWithFrame:NSMakeRect(0, 0, BTN_W, BTN_H)] autorelease];
	[b setIcon:icon];
	[b setTarget:target];
	[b setAction:action];
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
	[win setMinSize:NSMakeSize(420, 240)];
	if (FloAppIcon() != nil)
		[win setMiniwindowImage:FloAppIcon()];

	NSView *cv = [win contentView];
	band = [[[FloToolbarBand alloc] initWithFrame:NSMakeRect(0, 0, 100, BAR_H)] autorelease];
	[cv addSubview:band];
	backBtn = makeButton(FloIconBack, self, @selector(goBack:), band);
	fwdBtn = makeButton(FloIconForward, self, @selector(goForward:), band);
	starBtn = makeButton(FloIconStar, self, @selector(bookmarkAction:), band);
	newTabBtn = makeButton(FloIconPlus, self, @selector(newTabAction:), band);
	addr = [[[FloAddressBar alloc] initWithTarget:self goAction:@selector(go:) reloadAction:@selector(reloadOrStop:)] autorelease];
	[band addSubview:addr];
	urlField = [addr field];

	strip = [[[FloTabStrip alloc] initWithFrame:NSMakeRect(0, 0, 100, TAB_H)] autorelease];
	[strip setBrowser:self];
	[strip setHidden:YES];
	[cv addSubview:strip];

	container = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)] autorelease];
	[cv addSubview:container];
	status = [[[FloStatusLabel alloc] initWithFrame:NSMakeRect(0, 0, 10, 20)] autorelease];
	[status setHidden:YES];
	[container addSubview:status];

	/* the find bar: hidden until Cmd-F */
	findBar = [[[FloToolbarBand alloc] initWithFrame:NSMakeRect(0, 0, 100, FIND_H)] autorelease];
	[findBar setHidden:YES];
	[cv addSubview:findBar];
	{
		NSTextField *lab = [[[NSTextField alloc] initWithFrame:NSMakeRect(12, 8, 40, 18)] autorelease];
		[lab setStringValue:@"Find:"];
		[lab setEditable:NO];
		[lab setSelectable:NO];
		[lab setBezeled:NO];
		[lab setDrawsBackground:NO];
		[lab setFont:[NSFont systemFontOfSize:12]];
		[findBar addSubview:lab];
	}
	findField = [[[NSTextField alloc] initWithFrame:NSMakeRect(54, 5, 300, 24)] autorelease];
	[findField setDelegate:(id)self];
	[findField setTarget:self];
	[findField setAction:@selector(findNextAction:)];       /* Return: the next match */
	[findBar addSubview:findField];
	findPrev = makeButton(FloIconBack, self, @selector(findPrevAction:), findBar);
	findNext = makeButton(FloIconForward, self, @selector(findNextAction:), findBar);
	findStatus = [[[NSTextField alloc] initWithFrame:NSMakeRect(0, 8, 80, 18)] autorelease];
	[findStatus setEditable:NO];
	[findStatus setSelectable:NO];
	[findStatus setBezeled:NO];
	[findStatus setDrawsBackground:NO];
	[findStatus setTextColor:[NSColor colorWithCalibratedRed:0.75 green:0.15 blue:0.15 alpha:1.0]];
	[findStatus setFont:[NSFont systemFontOfSize:12]];
	[findBar addSubview:findStatus];
	{
		NSButton *done = [[[NSButton alloc] initWithFrame:NSMakeRect(0, 5, 60, 24)] autorelease];
		[done setTitle:@"Done"];
		[done setTarget:self];
		[done setAction:@selector(hideFindAction:)];
		[done setTag:77];
		[findBar addSubview:done];
	}

	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(bookmarksChanged:)
		name:FloBookmarksChanged object:nil];
	[[FloBrowser all] addObject:self];
	[self relayout];
	/* Not shown yet: showing it runs delegate callbacks that ask the core about history, and the
	 * core has not finished building this browser window while it is calling us. Next loop pass. */
	[self performSelector:@selector(showWindow) withObject:nil afterDelay:0];
	return self;
}

- (void)dealloc
{
	[[NSNotificationCenter defaultCenter] removeObserver:self];
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
	CGFloat y = (BAR_H - BTN_H) / 2;

	[band setFrame:NSMakeRect(0, top, W, BAR_H)];
	[backBtn setFrame:NSMakeRect(10, y, BTN_W, BTN_H)];
	[fwdBtn setFrame:NSMakeRect(10 + BTN_W, y, BTN_W, BTN_H)];
	[newTabBtn setFrame:NSMakeRect(W - 10 - BTN_W, y, BTN_W, BTN_H)];
	[starBtn setFrame:NSMakeRect(W - 10 - 2 * BTN_W, y, BTN_W, BTN_H)];
	[addr setFrame:NSMakeRect(10 + 2 * BTN_W + 12, y, W - (10 + 2 * BTN_W + 12) - (10 + 2 * BTN_W + 12), BTN_H)];
	[strip setHidden:stripH == 0];
	[strip setFrame:NSMakeRect(0, top - stripH, W, stripH)];
	{
		CGFloat findH = findVisible ? FIND_H : 0, fy = top - stripH - findH;
		NSView *done = [findBar viewWithTag:77];

		[findBar setFrame:NSMakeRect(0, fy, W, findH)];
		[findBar setHidden:!findVisible];
		[findField setFrame:NSMakeRect(54, 5, MAX(120.0, MIN(360.0, W - 54 - 230)), 24)];
		[findPrev setFrame:NSMakeRect(NSMaxX([findField frame]) + 6, 4, BTN_W, BTN_H)];
		[findNext setFrame:NSMakeRect(NSMaxX([findField frame]) + 6 + BTN_W, 4, BTN_W, BTN_H)];
		[findStatus setFrame:NSMakeRect(NSMaxX([findNext frame]) + 8, 8, 80, 18)];
		[done setFrame:NSMakeRect(W - 72, 5, 60, 24)];
		[container setFrame:NSMakeRect(0, 0, W, fy)];
		if (findVisible) {              /* a view resized while hidden is not repainted by itself */
			[findBar setNeedsDisplay:YES];
			NSUInteger k;
			for (k = 0; k < [[findBar subviews] count]; k++)
				[[[findBar subviews] objectAtIndex:k] setNeedsDisplay:YES];
		}
	}
	if (current != nil)
		[current->scroll setFrame:[container bounds]];
	[strip setNeedsDisplay:YES];
}

- (void)windowDidResize:(NSNotification *)n { [self relayout]; }

/* the strip draws from `tabs`; these only ask it to repaint */
- (void)rebuildStrip { [strip setNeedsDisplay:YES]; }
- (void)layoutStrip { [strip setNeedsDisplay:YES]; }

/* ---- tabs ------------------------------------------------------------------- */

- (void)addTab:(FloTab *)t select:(BOOL)select
{
	t->browser = self;
	[tabs addObject:t];
	[container addSubview:t->scroll];
	[t->scroll setFrame:[container bounds]];       /* a valid size before the core first asks for it */
	[t->scroll setHidden:YES];
	[container addSubview:status positioned:NSWindowAbove relativeTo:nil];   /* the hover label stays on top */
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
	[status setText:nil];
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
	[addr setSecure:[current->url hasPrefix:@"https://"]];
	[backBtn setEnabled:flo_win_can_back(current->gw)];
	[fwdBtn setEnabled:flo_win_can_forward(current->gw)];
	[[addr reloadButton] setIcon:current->loading ? FloIconStop : FloIconReload];
	[starBtn setIcon:[[FloStore bookmarks] contains:current->url] ? FloIconStarFilled : FloIconStar];
	[starBtn setEnabled:[current->url length] > 0];
	[strip setNeedsDisplay:YES];
}

/* the hover label: shows what the core reports, then fades after a few seconds */
- (void)setStatusText:(NSString *)s
{
	[NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(hideStatus) object:nil];
	[status setText:s];
	if ([s length] > 0)
		[self performSelector:@selector(hideStatus) withObject:nil afterDelay:4.0];
}

- (void)hideStatus { [status setText:nil]; }

- (void)toggleBookmark
{
	FloStore *bm = [FloStore bookmarks];
	if (current == nil || [current->url length] == 0)
		return;
	if ([bm contains:current->url])
		[bm remove:current->url];
	else
		[bm add:current->url title:[current displayTitle]];
	[[NSNotificationCenter defaultCenter] postNotificationName:FloBookmarksChanged object:nil];
}

- (void)bookmarkAction:(id)sender { [self toggleBookmark]; }
- (void)bookmarksChanged:(NSNotification *)n { [self setNeedsChrome]; }

/* ---- find in page, zoom ---------------------------------------------------------- */

- (void)showFind
{
	findVisible = YES;
	[self relayout];
	[win makeFirstResponder:findField];
	[findField selectText:nil];
	if ([[findField stringValue] length] > 0)
		[self runFind:YES];
}

- (void)hideFind
{
	if (!findVisible)
		return;
	findVisible = NO;
	if (current != nil && current->gw != NULL)
		flo_win_find_clear(current->gw);
	[findStatus setStringValue:@""];
	[self relayout];
	if (current != nil)
		[win makeFirstResponder:current->page];
}

- (void)runFind:(BOOL)forwards
{
	NSString *t = [findField stringValue];
	if (current == nil || current->gw == NULL)
		return;
	if ([t length] == 0)
		[findStatus setStringValue:@""];
	flo_win_find(current->gw, [t UTF8String], forwards, NO);
}

- (void)findNextAction:(id)s { [self runFind:YES]; }
- (void)findPrevAction:(id)s { [self runFind:NO]; }
- (void)hideFindAction:(id)s { [self hideFind]; }
- (void)controlTextDidChange:(NSNotification *)n { if ([n object] == findField) [self runFind:YES]; }

- (BOOL)control:(NSControl *)c textView:(NSTextView *)tv doCommandBySelector:(SEL)sel
{
	if (c == findField && sel == @selector(cancelOperation:)) {      /* Escape */
		[self hideFind];
		return YES;
	}
	return NO;
}

- (void)setFindFound:(BOOL)found
{
	[findStatus setStringValue:(!found && [[findField stringValue] length] > 0) ? @"Not found" : @""];
}

- (void)zoom:(int)step
{
	if (current != nil && current->gw != NULL)
		flo_win_zoom(current->gw, step);
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
