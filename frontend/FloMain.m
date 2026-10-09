/*
 * Florence: main(), the application delegate and the menus. GNUstep's run loop is the one loop of the process; WebKit's GLib
 * main context is pumped from it (FloGLib.m). The windows and tabs themselves are FloBrowser.m and FloTab.m, the page view
 * FloPageView.m.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#import <AppKit/AppKit.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#import "FloBrowser.h"
#import "FloStore.h"
#import "FloPrefs.h"
#import "FloAbout.h"
#import "FloGLib.h"
#include "flo.h"

static NSString *startURL;

/* downloads (the engine saves them; this only says so, in the status label of the window that has the keyboard) */
static void download_event(void *ctx, int state, const char *name, double fraction)
{
	static int last = -1;
	FloBrowser *b = [FloBrowser key];
	NSString *n = [NSString stringWithUTF8String:name != NULL ? name : ""];
	NSString *msg = nil;

	(void)ctx;
	switch (state) {
	case FLO_DOWNLOAD_STARTED: last = -1; msg = @"Downloading..."; break;
	case FLO_DOWNLOAD_PROGRESS:
		if ((int)(fraction * 100) == last)
			return;
		last = (int)(fraction * 100);
		msg = [NSString stringWithFormat:@"Downloading %@ %d%%", n, last];
		break;
	case FLO_DOWNLOAD_FINISHED: msg = [NSString stringWithFormat:@"Downloaded %@", n]; break;
	case FLO_DOWNLOAD_FAILED: msg = [NSString stringWithFormat:@"Download of %@ failed", n]; break;
	}
	[b setStatusText:msg];
}
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
			title = [FloPrefix(title, 55) stringByAppendingString:@"..."];
		mi = [m addItemWithTitle:title action:@selector(openStored:) keyEquivalent:@""];
		[mi setTarget:menuTarget];
		[mi setRepresentedObject:[e objectAtIndex:0]];
	}
}

/* ---- application ------------------------------------------------------------ */

@interface FloApp : NSObject <NSApplicationDelegate>
- (void)refreshStoreMenus;
@end

@implementation FloApp

- (void)applicationDidFinishLaunching:(NSNotification *)n
{
	NSString *list = [[NSBundle mainBundle] pathForResource:@"blocklist-default" ofType:@"json"];

	[[FloGLib shared] start];
	flo_engine_set_download_handler(download_event, NULL);
	if (list != nil)
		flo_engine_set_blocklist([list UTF8String]);
	[FloBrowser openWindowWithAddress:startURL];
	/* FLORENCE_AUTOTAB=<seconds>: open a new tab after that long, for watching the redraw (tests/pi: screenshots at short intervals) */
	if (getenv("FLORENCE_AUTOTAB") != NULL)
		[self performSelector:@selector(autoTab:) withObject:nil afterDelay:atof(getenv("FLORENCE_AUTOTAB"))];
}

- (void)autoTab:(id)s { [[FloBrowser key] newTab]; }

/* GNUstep offers every command-line argument that is not an option to the delegate as a file to open, and shows an alert when
 * there is no such method. The address on the command line is opened by applicationDidFinishLaunching; this only accepts it. */
- (BOOL)application:(NSApplication *)app openFile:(NSString *)name { return YES; }

- (void)applicationWillTerminate:(NSNotification *)n
{
	NSArray *all = [[[FloBrowser all] copy] autorelease];
	NSUInteger i, j;
	for (i = 0; i < [all count]; i++) {
		FloBrowser *b = [all objectAtIndex:i];
		NSArray *ts = [[b->tabs copy] autorelease];
		b->closing = YES;
		for (j = 0; j < [ts count]; j++)
			[[ts objectAtIndex:j] teardown];
	}
	flo_engine_fini();
}

- (void)newWindow:(id)s { [FloBrowser openWindowWithAddress:nil]; }
- (void)newTab:(id)s { [[FloBrowser key] newTab]; }
- (void)closeTab:(id)s { [[FloBrowser key] closeCurrentTab]; }
- (void)closeWindow:(id)s { [[NSApp keyWindow] performClose:nil]; }
- (void)openLocation:(id)s { [[FloBrowser key] focusLocation]; }
- (void)reload:(id)s { FloBrowser *b = [FloBrowser key]; if (b && b->current && b->current->fp) flo_page_reload(b->current->fp); }
- (void)stopLoading:(id)s { FloBrowser *b = [FloBrowser key]; if (b && b->current && b->current->fp) flo_page_stop(b->current->fp); }
- (void)goBack:(id)s { [[FloBrowser key] goBack:s]; }
- (void)goForward:(id)s { [[FloBrowser key] goForward:s]; }
- (void)bookmarkPage:(id)s { [[FloBrowser key] toggleBookmark]; }

- (void)openStored:(id)item
{
	FloBrowser *b = [FloBrowser key];
	NSString *addr = [item representedObject];
	if (b != nil && b->current != nil && b->current->fp != NULL && addr != nil)
		flo_page_load(b->current->fp, [addr UTF8String]);
}

- (void)clearHistory:(id)s
{
	[[FloStore history] clear];
	[self refreshStoreMenus];
}

- (void)refreshStoreMenus
{
	fillStoreMenu(bookmarksMenu, [FloStore bookmarks], 2, 60);
	fillStoreMenu(historyMenu, [FloStore history], 2, 25);
}

- (void)showFind:(id)s { [[FloBrowser key] showFind]; }
- (void)findNext:(id)s { [[FloBrowser key] runFind:YES]; }
- (void)findPrevious:(id)s { [[FloBrowser key] runFind:NO]; }
- (void)zoomIn:(id)s { [[FloBrowser key] zoom:1]; }
- (void)zoomOut:(id)s { [[FloBrowser key] zoom:-1]; }
- (void)zoomReset:(id)s { [[FloBrowser key] zoom:0]; }

- (void)reloadCurrent
{
	FloBrowser *b = [FloBrowser key];
	if (b != nil && b->current != nil && b->current->fp != NULL)
		flo_page_reload(b->current->fp);        /* these settings apply to a page when it is loaded */
}

- (void)toggleHideAds:(id)s { flo_opt_set_hide_ads(!flo_opt_hide_ads()); [self reloadCurrent]; }
- (void)showAbout:(id)s { [[FloAbout shared] show]; }
- (void)showPrefs:(id)s { [[FloPrefs shared] show]; }
- (void)setMinFont:(id)item { flo_opt_set_font_min((int)[item tag]); [self reloadCurrent]; }

- (void)toggleJS:(id)s
{
	flo_engine_set_javascript(!flo_engine_javascript());
	[self reloadCurrent];                   /* the page must be loaded again to run (or not run) scripts */
}

- (void)nextTab:(id)s { [[FloBrowser key] nextTab:1]; }
- (void)previousTab:(id)s { [[FloBrowser key] nextTab:-1]; }

- (BOOL)validateMenuItem:(NSMenuItem *)item
{
	FloBrowser *b = [FloBrowser key];
	FloTab *t = b != nil ? b->current : nil;
	SEL a = [item action];
	if (a == @selector(toggleJS:)) {
		[item setState:flo_engine_javascript() ? NSOnState : NSOffState];
		return b != nil;
	}
	if (a == @selector(toggleHideAds:)) {
		[item setState:flo_opt_hide_ads() ? NSOnState : NSOffState];
		return YES;
	}
	if (a == @selector(setMinFont:)) {
		[item setState:flo_opt_font_min() == [item tag] ? NSOnState : NSOffState];
		return YES;
	}
	if (a == @selector(newWindow:) || a == @selector(showPrefs:) || a == @selector(showAbout:) || a == @selector(terminate:) ||
	    a == @selector(openStored:) || a == @selector(clearHistory:))
		return YES;
	if (t == nil || t->fp == NULL)
		return NO;
	if (a == @selector(goBack:))
		return t->canBack;
	if (a == @selector(goForward:))
		return t->canForward;
	if (a == @selector(nextTab:) || a == @selector(previousTab:))
		return [b->tabs count] > 1;
	if (a == @selector(findNext:) || a == @selector(findPrevious:))
		return b->findVisible;
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
	addItem(m, @"About Florence", @selector(showAbout:), @"", app);
	[m addItem:[NSMenuItem separatorItem]];
	addItem(m, @"Preferences...", @selector(showPrefs:), @",", app);
	[m addItem:[NSMenuItem separatorItem]];
	addItem(m, @"Quit Florence", @selector(terminate:), @"q", NSApp);

	m = addSubmenu(bar, @"File");
	addItem(m, @"New Window", @selector(newWindow:), @"n", app);
	addItem(m, @"New Tab", @selector(newTab:), @"t", app);
	addItem(m, @"Open Location...", @selector(openLocation:), @"l", app);
	addItem(m, @"Close Tab", @selector(closeTab:), @"w", app);
	addItem(m, @"Close Window", @selector(closeWindow:), @"W", app);

	m = addSubmenu(bar, @"Edit");           /* target nil: the first responder (the address field) */
	addItem(m, @"Cut", @selector(cut:), @"x", nil);
	addItem(m, @"Copy", @selector(copy:), @"c", nil);
	addItem(m, @"Paste", @selector(paste:), @"v", nil);
	addItem(m, @"Select All", @selector(selectAll:), @"a", nil);
	[m addItem:[NSMenuItem separatorItem]];
	addItem(m, @"Find...", @selector(showFind:), @"f", app);
	addItem(m, @"Find Next", @selector(findNext:), @"g", app);
	addItem(m, @"Find Previous", @selector(findPrevious:), @"G", app);

	m = addSubmenu(bar, @"Go");
	addItem(m, @"Back", @selector(goBack:), @"[", app);
	addItem(m, @"Forward", @selector(goForward:), @"]", app);
	addItem(m, @"Reload", @selector(reload:), @"r", app);
	addItem(m, @"Stop", @selector(stopLoading:), @".", app);
	addItem(m, @"Next Tab", @selector(nextTab:), @"}", app);
	addItem(m, @"Previous Tab", @selector(previousTab:), @"{", app);

	m = addSubmenu(bar, @"View");
	addItem(m, @"Enable JavaScript", @selector(toggleJS:), @"", app);
	[m addItem:[NSMenuItem separatorItem]];
	addItem(m, @"Zoom In", @selector(zoomIn:), @"+", app);
	addItem(m, @"Zoom Out", @selector(zoomOut:), @"-", app);
	addItem(m, @"Actual Size", @selector(zoomReset:), @"0", app);
	[m addItem:[NSMenuItem separatorItem]];
	addItem(m, @"Block Ads and Trackers", @selector(toggleHideAds:), @"", app);
	{
		NSMenu *sub = addSubmenu(m, @"Minimum Font Size");
		static const struct { const char *title; int tenths; } sizes[] = {
			{ "Default", 85 }, { "10 pt", 100 }, { "12 pt", 120 }, { "14 pt", 140 }, { "16 pt", 160 } };
		int i;

		for (i = 0; i < 5; i++)
			[addItem(sub, [NSString stringWithUTF8String:sizes[i].title], @selector(setMinFont:), @"", app)
				setTag:sizes[i].tenths];
	}

	m = addSubmenu(bar, @"Bookmarks");
	bookmarksMenu = m;
	menuTarget = app;
	addItem(m, @"Bookmark This Page", @selector(bookmarkPage:), @"d", app);
	[m addItem:[NSMenuItem separatorItem]];

	m = addSubmenu(bar, @"History");
	historyMenu = m;
	addItem(m, @"Clear History", @selector(clearHistory:), @"", app);
	[m addItem:[NSMenuItem separatorItem]];

	[[NSNotificationCenter defaultCenter] addObserver:app selector:@selector(refreshStoreMenus)
		name:FloBookmarksChanged object:nil];
	[[NSNotificationCenter defaultCenter] addObserver:app selector:@selector(refreshStoreMenus)
		name:FloHistoryChanged object:nil];
	[NSApp setMainMenu:bar];
	[app refreshStoreMenus];
}

/* mkdir -p: the engine opens its data and cache directories but does not make the path to them */
static void make_dirs(const char *path)
{
	char buf[1024], *p;

	snprintf(buf, sizeof buf, "%s", path);
	for (p = buf + 1; *p != '\0'; p++)
		if (*p == '/') {
			*p = '\0';
			mkdir(buf, 0700);
			*p = '/';
		}
	mkdir(buf, 0700);
}

int main(int argc, char **argv)
{
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
	const char *home = getenv("HOME");
	char data[1024], cache[1024], conf[1024];
	FloApp *app;
	int i;

	/* There is no terminal to read when Florence is started from the file manager or openapp, and the reports below (and NSLog)
	 * go to stderr: keep them in ~/.florence/florence.log, the last run's only. FLORENCE_STDERR=1 leaves stderr alone. */
	if (getenv("FLORENCE_STDERR") == NULL) {
		char dir[1024], log[1100];

		snprintf(dir, sizeof dir, "%s/.florence", home != NULL ? home : "/tmp");
		make_dirs(dir);
		snprintf(log, sizeof log, "%s/florence.log", dir);
		freopen(log, "w", stderr);
		setvbuf(stderr, NULL, _IOLBF, 0);
	}
	flo_install_crash_report();
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
	[NSApplication sharedApplication];
	flo_install_xio_handler();
	loadIcon();
	snprintf(data, sizeof data, "%s/.florence/data", home != NULL ? home : "/tmp");
	snprintf(cache, sizeof cache, "%s/.florence/cache", home != NULL ? home : "/tmp");
	snprintf(conf, sizeof conf, "%s/.florence/Florence.conf", home != NULL ? home : "/tmp");
	make_dirs(data);
	make_dirs(cache);
	flo_prefs_init(conf);
	if (flo_engine_init(data, cache) != 0)
		return 1;
	app = [[FloApp alloc] init];
	[NSApp setDelegate:app];
	buildMenus(app);
	[NSApp run];
	[pool release];
	return 0;
}
