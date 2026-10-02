/*
 * Florence: the GNUstep user interface shell: the pump that runs NetSurf's scheduler, the
 * flo_ui_* bridge from the C glue onto tabs, the clipboard, the menus and main(). The windows
 * and tabs themselves are FloBrowser.m and FloTab.m, the page view FloPage.m.
 *
 * Built for little machines: the pump is a one-shot timer that sleeps exactly until the next
 * scheduled NetSurf callback (nothing runs while the page is idle), resizes are debounced so a
 * window drag reflows the page once, and the scroll view blits already-painted pixels.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#import <AppKit/AppKit.h>
#include <string.h>
#include <stdlib.h>
#import "FloBrowser.h"
#import "FloStore.h"
#include "gnustep/gs.h"

static NSString *startURL;
static NSImage *appIcon;                /* the Florentine giglio, from the bundle's Resources */

NSImage *FloAppIcon(void) { return appIcon; }

static void loadIcon(void)
{
	NSString *path = [[NSBundle mainBundle] pathForResource:@"Florence" ofType:@"tiff"];
	if (path == nil)
		path = [[NSBundle mainBundle] pathForResource:@"Florence" ofType:@"png"];
	if (path != nil)
		appIcon = [[NSImage alloc] initWithContentsOfFile:path];
	if (appIcon != nil)
		[NSApp setApplicationIconImage:appIcon];
}

/* ---- the scheduler pump -------------------------------------------------- */

@interface FloPump : NSObject {
	NSTimer *timer;
	BOOL inTick;
}
+ (FloPump *)shared;
- (void)wake;
@end

@implementation FloPump

+ (FloPump *)shared
{
	static FloPump *p;
	if (p == nil)
		p = [[FloPump alloc] init];
	return p;
}

- (void)arm:(NSTimeInterval)sec
{
	[timer invalidate];
	[timer release];
	timer = [[NSTimer timerWithTimeInterval:sec target:self selector:@selector(tick:)
		userInfo:nil repeats:NO] retain];
	NSRunLoop *rl = [NSRunLoop currentRunLoop];
	[rl addTimer:timer forMode:NSDefaultRunLoopMode];
	[rl addTimer:timer forMode:NSEventTrackingRunLoopMode];   /* keep loading during menus/drags */
}

- (void)tick:(NSTimer *)t
{
	int ms;
	inTick = YES;
	ms = flo_schedule_run();
	inTick = NO;
	/* sleep until the next callback; the floor stops a 1 ms poll loop from eating a slow CPU */
	[self arm:ms < 0 ? 0.25 : (ms < 4 ? 0.004 : (ms > 250 ? 0.25 : ms / 1000.0))];
}

- (void)wake
{
	if (inTick)
		return;                                 /* tick re-arms itself when it returns */
	if (timer != nil && [timer isValid] && [[timer fireDate] timeIntervalSinceNow] < 0.02)
		return;                                 /* already due soon */
	[self arm:0.001];
}

@end

void flo_ui_wake(void) { [[FloPump shared] wake]; }
void flo_ui_quit(void) { [NSApp terminate:nil]; }

/* ---- the flo_ui_* functions the C glue calls ------------------------------- */

#define T(ui) ((FloTab *)(ui))

void *flo_ui_window_new(struct gui_window *gw, void *existing_ui, int flags)
{
	FloTab *tab = [[FloTab alloc] initWithGuiWindow:gw];    /* owned by the gui_window */
	FloBrowser *b = nil;

	if ((flags & FLO_NEW_TAB) && existing_ui != NULL)
		b = T(existing_ui)->browser;
	if (b == nil)
		b = [[[FloBrowser alloc] init] autorelease];    /* the browsers list keeps it */
	/* a first tab always shows; a later one only if the core asked for the foreground */
	[b addTab:tab select:b->current == nil || (flags & FLO_NEW_FOREGROUND) != 0];
	if (flags & FLO_NEW_FOCUS_LOCATION)
		[b performSelector:@selector(focusLocation) withObject:nil afterDelay:0.05];
	return tab;
}

void flo_ui_window_free(void *ui)
{
	FloTab *t = T(ui);
	[t->browser removeTab:t];
	[t teardown];
	[t release];
}

void flo_ui_invalidate(void *ui, int x0, int y0, int x1, int y1)
{
	if (x1 < 0)
		[T(ui)->page setNeedsDisplay:YES];
	else
		[T(ui)->page invalidatePageRect:NSMakeRect(x0, y0, x1 - x0, y1 - y0)];
}

void flo_ui_get_scroll(void *ui, int *x, int *y)
{
	NSPoint o = [[T(ui)->scroll contentView] bounds].origin;
	*x = (int)o.x;
	*y = (int)o.y;
}

void flo_ui_set_scroll(void *ui, int x, int y)
{
	NSClipView *clip = [T(ui)->scroll contentView];
	[clip scrollToPoint:[clip constrainScrollPoint:NSMakePoint(x, y)]];
	[T(ui)->scroll reflectScrolledClipView:clip];
}

void flo_ui_get_viewport(void *ui, int *w, int *h)
{
	NSSize s = [T(ui)->scroll contentSize];
	*w = (int)s.width;
	*h = (int)s.height;
}

void flo_ui_update_extent(void *ui) { [T(ui) updateExtent]; }

void flo_ui_set_title(void *ui, const char *title)
{
	FloTab *t = T(ui);
	if (title == NULL)
		return;
	[t setTitle:[NSString stringWithUTF8String:title]];
	[t->browser setNeedsChrome];
}

void flo_ui_set_url(void *ui, const char *url)
{
	FloTab *t = T(ui);
	if (url == NULL)
		return;
	[t setUrl:[NSString stringWithUTF8String:url]];
	[t->browser setNeedsChrome];
}

void flo_ui_set_status(void *ui, const char *text)
{
	FloTab *t = T(ui);
	[t setStatus:text != NULL ? [NSString stringWithUTF8String:text] : @""];
	if (t->browser != nil && t->browser->current == t)      /* hover text: update at once */
		[t->browser->status setStringValue:t->status];
}

void flo_ui_set_pointer(void *ui, int p) { [T(ui)->page setPointer:p]; }

static void refreshHistoryMenu(void);

void flo_ui_throbber(void *ui, bool on)
{
	FloTab *t = T(ui);
	t->loading = on;
	[t->browser setNeedsChrome];
	/* the page is done: it belongs in the history (not internal pages, not blanks) */
	if (!on && [t->url length] > 0 && ![t->url hasPrefix:@"about:"]) {
		[[FloStore history] add:t->url title:[t displayTitle]];
		refreshHistoryMenu();
	}
}

void flo_ui_place_caret(void *ui, int x, int y, int height)
{
	[T(ui)->page placeCaret:NSMakeRect(x, y, 1, height)];
}

void flo_ui_remove_caret(void *ui) { [T(ui)->page removeCaret]; }

char *flo_ui_clipboard_get(size_t *len)
{
	NSString *s = [[NSPasteboard generalPasteboard] stringForType:NSStringPboardType];
	const char *u = [s UTF8String];
	char *r;
	if (u == NULL)
		return NULL;
	*len = strlen(u);
	r = malloc(*len + 1);
	if (r != NULL)
		memcpy(r, u, *len + 1);
	return r;
}

void flo_ui_clipboard_set(const char *text, size_t len)
{
	NSString *s = [[[NSString alloc] initWithBytes:text length:len encoding:NSUTF8StringEncoding] autorelease];
	NSPasteboard *pb = [NSPasteboard generalPasteboard];
	if (s == nil)
		return;
	[pb declareTypes:[NSArray arrayWithObject:NSStringPboardType] owner:nil];
	[pb setString:s forType:NSStringPboardType];
}

/* ---- bookmark and history menus ----------------------------------------------- */

static NSMenu *bookmarksMenu, *historyMenu;
static id menuTarget;                   /* the FloApp */

/* The first `fixed` items of the menu stay; the rest are rebuilt from the store. */
static void fillStoreMenu(NSMenu *m, FloStore *st, NSUInteger fixed, NSUInteger limit)
{
	NSArray *items = [st items];
	NSUInteger i;
	if (m == nil)
		return;
	while ([m numberOfItems] > (NSInteger)fixed)
		[m removeItemAtIndex:fixed];
	for (i = 0; i < [items count] && i < limit; i++) {
		NSArray *e = [items objectAtIndex:i];
		NSString *title = [[e objectAtIndex:1] length] > 0 ? [e objectAtIndex:1] : [e objectAtIndex:0];
		NSMenuItem *mi;
		if ([title length] > 56)
			title = [[title substringToIndex:55] stringByAppendingString:@"..."];
		mi = [m addItemWithTitle:title action:@selector(openStored:) keyEquivalent:@""];
		[mi setTarget:menuTarget];
		[mi setRepresentedObject:[e objectAtIndex:0]];
	}
}

static void refreshHistoryMenu(void) { fillStoreMenu(historyMenu, [FloStore history], 2, 25); }

/* ---- application ------------------------------------------------------------ */

@interface FloApp : NSObject
- (void)refreshStoreMenus;
@end

@implementation FloApp

- (void)applicationDidFinishLaunching:(NSNotification *)n
{
	flo_trace("app: did finish launching");
	flo_open_url(startURL != nil ? [startURL UTF8String] : NULL);
	flo_ui_wake();
}

- (void)applicationWillTerminate:(NSNotification *)n
{
	NSArray *all = [[[FloBrowser all] copy] autorelease];
	NSUInteger i, j;
	for (i = 0; i < [all count]; i++) {
		FloBrowser *b = [all objectAtIndex:i];
		NSArray *ts = [[b->tabs copy] autorelease];
		b->closing = YES;
		for (j = 0; j < [ts count]; j++) {
			FloTab *t = [ts objectAtIndex:j];
			if (t->gw != NULL)
				flo_win_close(t->gw);
		}
	}
	flo_core_fini();
}

- (void)newWindow:(id)s { flo_open_url(NULL); }
- (void)newTab:(id)s { [[FloBrowser key] newTab]; }
- (void)closeTab:(id)s { [[FloBrowser key] closeCurrentTab]; }
- (void)closeWindow:(id)s { [[NSApp keyWindow] performClose:nil]; }
- (void)openLocation:(id)s { [[FloBrowser key] focusLocation]; }
- (void)reload:(id)s { FloBrowser *b = [FloBrowser key]; if (b && b->current && b->current->gw) flo_win_reload(b->current->gw); }
- (void)stopLoading:(id)s { FloBrowser *b = [FloBrowser key]; if (b && b->current && b->current->gw) flo_win_stop(b->current->gw); }
- (void)goBack:(id)s { [[FloBrowser key] goBack:s]; }
- (void)goForward:(id)s { [[FloBrowser key] goForward:s]; }
- (void)bookmarkPage:(id)s
{
	FloBrowser *b = [FloBrowser key];
	FloTab *t = b != nil ? b->current : nil;
	FloStore *bm = [FloStore bookmarks];
	if (t == nil || [t->url length] == 0)
		return;
	if ([bm contains:t->url])
		[bm remove:t->url];
	else
		[bm add:t->url title:[t displayTitle]];
	[self refreshStoreMenus];
}

- (void)openStored:(id)item
{
	FloBrowser *b = [FloBrowser key];
	NSString *addr = [item representedObject];
	if (b != nil && b->current != nil && b->current->gw != NULL && addr != nil)
		flo_win_navigate(b->current->gw, [addr UTF8String]);
}

- (void)clearHistory:(id)s
{
	[[FloStore history] clear];
	refreshHistoryMenu();
}

- (void)refreshStoreMenus
{
	fillStoreMenu(bookmarksMenu, [FloStore bookmarks], 2, 60);
}

- (void)toggleJS:(id)s
{
	FloBrowser *b = [FloBrowser key];
	flo_js_set(!flo_js_enabled());
	if (b != nil && b->current != nil && b->current->gw != NULL)
		flo_win_reload(b->current->gw);         /* the page must be loaded again to run (or not run) scripts */
}

- (void)nextTab:(id)s { [[FloBrowser key] nextTab:1]; }
- (void)previousTab:(id)s { [[FloBrowser key] nextTab:-1]; }

- (BOOL)validateMenuItem:(NSMenuItem *)item
{
	FloBrowser *b = [FloBrowser key];
	FloTab *t = b != nil ? b->current : nil;
	SEL a = [item action];
	if (a == @selector(toggleJS:)) {
		[item setTitle:flo_js_available() ? @"Enable JavaScript" : @"JavaScript (not in this build)"];
		[item setState:flo_js_enabled() ? NSOnState : NSOffState];
		return flo_js_available() && b != nil;
	}
	if (a == @selector(newWindow:) || a == @selector(terminate:) || a == @selector(openStored:) ||
	    a == @selector(clearHistory:))
		return YES;
	if (t == nil || t->gw == NULL)
		return NO;
	if (a == @selector(goBack:))
		return flo_win_can_back(t->gw);
	if (a == @selector(goForward:))
		return flo_win_can_forward(t->gw);
	if (a == @selector(nextTab:) || a == @selector(previousTab:))
		return [b->tabs count] > 1;
	if (a == @selector(bookmarkPage:)) {
		[item setTitle:[[FloStore bookmarks] contains:t->url] ? @"Remove Bookmark" : @"Bookmark This Page"];
		return [t->url length] > 0;
	}
	return YES;
}

@end

static NSMenuItem *addItem(NSMenu *m, NSString *title, SEL a, NSString *key, id target)
{
	NSMenuItem *i = [m addItemWithTitle:title action:a keyEquivalent:key];
	[i setTarget:target];
	return i;
}

static NSMenu *addSubmenu(NSMenu *bar, NSString *title)
{
	NSMenuItem *i = [bar addItemWithTitle:title action:NULL keyEquivalent:@""];
	NSMenu *m = [[[NSMenu alloc] initWithTitle:title] autorelease];
	[bar setSubmenu:m forItem:i];
	return m;
}

static void buildMenus(FloApp *app)
{
	NSMenu *bar = [[[NSMenu alloc] initWithTitle:@"Florence"] autorelease];
	NSMenu *m = addSubmenu(bar, @"Florence");
	addItem(m, @"Quit Florence", @selector(terminate:), @"q", NSApp);

	m = addSubmenu(bar, @"File");
	addItem(m, @"New Window", @selector(newWindow:), @"n", app);
	addItem(m, @"New Tab", @selector(newTab:), @"t", app);
	addItem(m, @"Open Location...", @selector(openLocation:), @"l", app);
	addItem(m, @"Close Tab", @selector(closeTab:), @"w", app);
	addItem(m, @"Close Window", @selector(closeWindow:), @"W", app);

	m = addSubmenu(bar, @"Edit");           /* target nil: the first responder (URL field or page) */
	addItem(m, @"Cut", @selector(cut:), @"x", nil);
	addItem(m, @"Copy", @selector(copy:), @"c", nil);
	addItem(m, @"Paste", @selector(paste:), @"v", nil);
	addItem(m, @"Select All", @selector(selectAll:), @"a", nil);

	m = addSubmenu(bar, @"Go");
	addItem(m, @"Back", @selector(goBack:), @"[", app);
	addItem(m, @"Forward", @selector(goForward:), @"]", app);
	addItem(m, @"Reload", @selector(reload:), @"r", app);
	addItem(m, @"Stop", @selector(stopLoading:), @".", app);
	addItem(m, @"Next Tab", @selector(nextTab:), @"}", app);
	addItem(m, @"Previous Tab", @selector(previousTab:), @"{", app);

	m = addSubmenu(bar, @"View");
	addItem(m, @"Enable JavaScript", @selector(toggleJS:), @"", app);

	m = addSubmenu(bar, @"Bookmarks");
	bookmarksMenu = m;
	menuTarget = app;
	addItem(m, @"Bookmark This Page", @selector(bookmarkPage:), @"d", app);
	[m addItem:[NSMenuItem separatorItem]];

	m = addSubmenu(bar, @"History");
	historyMenu = m;
	addItem(m, @"Clear History", @selector(clearHistory:), @"", app);
	[m addItem:[NSMenuItem separatorItem]];

	[NSApp setMainMenu:bar];
	[app refreshStoreMenus];
	refreshHistoryMenu();
}

int main(int argc, char **argv)
{
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
	FloApp *app;
	int i;

	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-') {
			startURL = [FloURLFromInput([NSString stringWithUTF8String:argv[i]]) retain];
			break;
		}
	}
#ifdef GNUSTEP
	/* no NSApplicationMain here: tell gnustep-base the arguments and environment ourselves
	 * (Darwin has no /proc/self to find them from) */
	{ extern char **environ; GSInitializeProcess(argc, argv, environ); }
#endif
	flo_trace("main: gnustep initialised");
	[NSApplication sharedApplication];
	flo_trace("main: NSApplication");
	loadIcon();
	if (flo_core_init(argc, argv) != 0)
		return 1;
	flo_trace("main: core ready, building menus");
	app = [[FloApp alloc] init];
	[NSApp setDelegate:app];
	buildMenus(app);
	flo_trace("main: run loop");
	[NSApp run];
	[pool release];
	return 0;
}
