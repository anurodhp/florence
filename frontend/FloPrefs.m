/* Florence: the Preferences window. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 *
 * Plain AppKit controls laid out by hand: four tabs in one small window, nothing running while it is
 * closed. Settings take effect as they are changed; the few that need a restart say so. */
#import "FloPrefs.h"
#import "FloStore.h"
#include "flo.h"

static const struct { const char *name, *prefix; } engines[] = {
	{ "DuckDuckGo", "https://html.duckduckgo.com/html/?q=" },       /* the light page */
	{ "Google", "https://www.google.com/search?q=" },
	{ "Bing", "https://www.bing.com/search?q=" },
	{ "Startpage", "https://www.startpage.com/do/search?q=" },
	{ "Wikipedia", "https://en.wikipedia.org/w/index.php?search=" },
};
#define NENGINES ((int)(sizeof(engines) / sizeof(engines[0])))

NSArray *FloSearchEngineNames(void)
{
	NSMutableArray *a = [NSMutableArray array];
	int i;
	for (i = 0; i < NENGINES; i++)
		[a addObject:[NSString stringWithUTF8String:engines[i].name]];
	return a;
}

NSString *FloSearchURLPrefix(void)
{
	const char *want = flo_pref_get("search", engines[0].name);
	int i;
	for (i = 0; i < NENGINES; i++)
		if (strcmp(engines[i].name, want) == 0)
			return [NSString stringWithUTF8String:engines[i].prefix];
	return [NSString stringWithUTF8String:engines[0].prefix];
}

#define WIN_W 500.0
#define WIN_H 330.0
#define LABEL_W 150.0
#define FIELD_X 170.0

static NSTextField *label(NSView *in, NSString *text, NSRect r, NSTextAlignment al)
{
	NSTextField *t = [[[NSTextField alloc] initWithFrame:r] autorelease];
	[t setStringValue:text];
	[t setEditable:NO];
	[t setSelectable:NO];
	[t setBezeled:NO];
	[t setDrawsBackground:NO];
	[t setAlignment:al];
	[t setFont:[NSFont systemFontOfSize:12]];
	[in addSubview:t];
	return t;
}

static NSButton *button(NSView *in, NSString *title, NSButtonType type, NSRect r, id target, SEL action)
{
	NSButton *b = [[[NSButton alloc] initWithFrame:r] autorelease];
	[b setButtonType:type];
	if (type == NSMomentaryPushInButton)
		[b setBezelStyle:NSRoundedBezelStyle];
	[b setTitle:title];
	[b setTarget:target];
	[b setAction:action];
	[b setFont:[NSFont systemFontOfSize:12]];
	[in addSubview:b];
	return b;
}

static NSPopUpButton *popup(NSView *in, NSArray *titles, NSRect r, id target, SEL action)
{
	NSPopUpButton *p = [[[NSPopUpButton alloc] initWithFrame:r pullsDown:NO] autorelease];
	[p addItemsWithTitles:titles];
	[p setTarget:target];
	[p setAction:action];
	[p setFont:[NSFont systemFontOfSize:12]];
	[in addSubview:p];
	return p;
}

static NSTextField *field(NSView *in, NSRect r, id delegate)
{
	NSTextField *t = [[[NSTextField alloc] initWithFrame:r] autorelease];
	[t setFont:[NSFont systemFontOfSize:12]];
	[t setDelegate:delegate];
	[in addSubview:t];
	return t;
}

@implementation FloPrefs

+ (FloPrefs *)shared
{
	static FloPrefs *p;
	if (p == nil)
		p = [[FloPrefs alloc] init];
	return p;
}

static NSString *pref(const char *key, const char *def)
{
	return [NSString stringWithUTF8String:flo_pref_get(key, def)];
}

- (NSView *)pane
{
	return [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, WIN_W - 40, WIN_H - 70)] autorelease];
}

- (void)build
{
	NSTabView *tabs;
	NSTabViewItem *ti;
	NSView *v;
	CGFloat h = WIN_H - 70, y;

	win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, WIN_W, WIN_H)
		styleMask:NSTitledWindowMask | NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
	[win setTitle:@"Florence Preferences"];
	[win setReleasedWhenClosed:NO];
	[win setDelegate:self];
	tabs = [[[NSTabView alloc] initWithFrame:NSMakeRect(10, 10, WIN_W - 20, WIN_H - 20)] autorelease];
	[[win contentView] addSubview:tabs];

	/* General */
	v = [self pane];
	y = h - 30;
	label(v, @"Homepage (blank: the start page):", NSMakeRect(0, y, LABEL_W + 60, 18), NSRightTextAlignment);
	homeField = field(v, NSMakeRect(FIELD_X + 70, y - 2, 200, 22), self);
	y -= 36;
	label(v, @"Search engine:", NSMakeRect(0, y, LABEL_W, 18), NSRightTextAlignment);
	enginePop = popup(v, FloSearchEngineNames(), NSMakeRect(FIELD_X, y - 3, 160, 24), self, @selector(changed:));
	y -= 36;
	label(v, @"Minimum font size:", NSMakeRect(0, y, LABEL_W, 18), NSRightTextAlignment);
	fontPop = popup(v, [NSArray arrayWithObjects:@"Default (8.5 pt)", @"10 pt", @"12 pt", @"14 pt", @"16 pt", nil],
		NSMakeRect(FIELD_X, y - 3, 160, 24), self, @selector(changed:));
	ti = [[[NSTabViewItem alloc] initWithIdentifier:@"general"] autorelease];
	[ti setLabel:@"General"];
	[ti setView:v];
	[tabs addTabViewItem:ti];

	/* Privacy */
	v = [self pane];
	y = h - 30;
	button(v, @"Clear History...", NSMomentaryPushInButton, NSMakeRect(20, y, 130, 26), self, @selector(clearHistory:));
	button(v, @"Clear Cookies...", NSMomentaryPushInButton, NSMakeRect(160, y, 130, 26), self, @selector(clearCookies:));
	y -= 34;
	privacyNote = label(v, @"", NSMakeRect(20, y - 40, 400, 54), NSLeftTextAlignment);
	[privacyNote setTextColor:[NSColor darkGrayColor]];
	ti = [[[NSTabViewItem alloc] initWithIdentifier:@"privacy"] autorelease];
	[ti setLabel:@"Privacy"];
	[ti setView:v];
	[tabs addTabViewItem:ti];

	/* Content blocking */
	v = [self pane];
	y = h - 30;
	adsBox = button(v, @"Block ads and trackers", NSSwitchButton, NSMakeRect(20, y, 400, 20), self, @selector(changed:));
	y -= 40;
	{
		NSTextField *t = label(v, @"The rules are Safari content-blocker lists, compiled by WebKit. Turning this on or off applies to pages "
			@"as they load.", NSMakeRect(20, y - 40, 400, 54), NSLeftTextAlignment);
		[t setTextColor:[NSColor darkGrayColor]];
	}
	ti = [[[NSTabViewItem alloc] initWithIdentifier:@"blocking"] autorelease];
	[ti setLabel:@"Content Blocking"];
	[ti setView:v];
	[tabs addTabViewItem:ti];

	/* Advanced */
	v = [self pane];
	y = h - 30;
	jsBox = button(v, @"Enable JavaScript", NSSwitchButton, NSMakeRect(20, y, 400, 20), self, @selector(changed:));
	y -= 40;
	{
		NSTextField *t = label(v, @"JavaScript applies to pages as they load. It costs memory and CPU on a small machine, so it is off until you turn it on.",
			NSMakeRect(20, y - 30, 400, 54), NSLeftTextAlignment);
		[t setTextColor:[NSColor darkGrayColor]];
	}
	ti = [[[NSTabViewItem alloc] initWithIdentifier:@"advanced"] autorelease];
	[ti setLabel:@"Advanced"];
	[ti setView:v];
	[tabs addTabViewItem:ti];
}

static int fontIndex(void)
{
	static const int sizes[] = { 85, 100, 120, 140, 160 };
	int i, f = flo_opt_font_min();
	for (i = 0; i < 5; i++)
		if (sizes[i] == f)
			return i;
	return 0;
}

- (void)refresh
{
	[homeField setStringValue:[NSString stringWithUTF8String:flo_opt_homepage()]];
	[enginePop selectItemWithTitle:pref("search", "DuckDuckGo")];
	if ([enginePop indexOfSelectedItem] < 0)
		[enginePop selectItemAtIndex:0];
	[fontPop selectItemAtIndex:fontIndex()];
	[adsBox setState:flo_opt_hide_ads() ? NSOnState : NSOffState];
	[jsBox setState:flo_engine_javascript() ? NSOnState : NSOffState];
}

- (void)show
{
	if (win == nil)
		[self build];
	[self refresh];
	[win center];
	[win makeKeyAndOrderFront:nil];
}

- (void)windowDidBecomeKey:(NSNotification *)n { [self refresh]; }

/* every control lands here: read them all, store what differs */
- (void)changed:(id)sender
{
	static const int sizes[] = { 85, 100, 120, 140, 160 };

	flo_pref_set("search", [[enginePop titleOfSelectedItem] UTF8String]);
	flo_opt_set_font_min(sizes[[fontPop indexOfSelectedItem]]);
	if (([adsBox state] == NSOnState) != flo_opt_hide_ads())
		flo_opt_set_hide_ads([adsBox state] == NSOnState);
	if (([jsBox state] == NSOnState) != flo_engine_javascript())
		flo_engine_set_javascript([jsBox state] == NSOnState);
	[self refresh];
}

/* text fields commit on Return or when focus leaves */
- (void)controlTextDidEndEditing:(NSNotification *)n
{
	id f = [n object];
	if (f == homeField) {
		NSString *s = [[homeField stringValue] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
		if ([s length] > 0 && [s rangeOfString:@"://"].location == NSNotFound && ![s hasPrefix:@"about:"])
			s = [@"http://" stringByAppendingString:s];
		flo_opt_set_homepage([s UTF8String]);
		[homeField setStringValue:s];
	}
}

- (BOOL)confirm:(NSString *)what
{
	return NSRunAlertPanel(what, @"This cannot be undone.", @"Clear", @"Cancel", nil) == NSAlertDefaultReturn;
}

- (void)clearHistory:(id)sender
{
	if (![self confirm:@"Clear browsing history?"])
		return;
	[[FloStore history] clear];
	[privacyNote setStringValue:@"History cleared."];
}

- (void)clearCookies:(id)sender
{
	if (![self confirm:@"Clear cookies?"])
		return;
	flo_clear_cookies();
	[privacyNote setStringValue:@"Cookies and other site data deleted."];
}

@end
