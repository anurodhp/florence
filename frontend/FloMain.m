/*
 * Florence: main(), the application delegate and the menus. GNUstep's run loop is the one loop of the
 * process; WebKit's GLib main context is pumped from it (FloGLib.m).
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only
 */
#import <AppKit/AppKit.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#import "FloWindow.h"
#import "FloGLib.h"
#include "flo.h"

@interface FloApp : NSObject <NSApplicationDelegate> {
	NSString *startURL;
}
- (id)initWithStartAddress:(NSString *)u;
- (void)newWindow:(id)s;
- (void)openLocation:(id)s;
- (void)goBack:(id)s;
- (void)goForward:(id)s;
- (void)reload:(id)s;
@end

static FloWindow *keyBrowser(void) { return [FloWindow key]; }

@implementation FloApp

- (id)initWithStartAddress:(NSString *)u
{
	if ((self = [super init]) != nil)
		startURL = [u retain];
	return self;
}

- (void)applicationDidFinishLaunching:(NSNotification *)n
{
	[[FloGLib shared] start];
	[[[FloWindow alloc] initWithAddress:startURL] release];     /* the list keeps it */
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(id)a { return YES; }
- (void)applicationWillTerminate:(NSNotification *)n { flo_engine_fini(); }

- (void)newWindow:(id)s { [[[FloWindow alloc] initWithAddress:nil] release]; }
- (void)openLocation:(id)s { [keyBrowser() focusAddress]; }
- (void)goBack:(id)s { [keyBrowser() goBack:s]; }
- (void)goForward:(id)s { [keyBrowser() goForward:s]; }
- (void)reload:(id)s { [keyBrowser() reloadOrStop:s]; }

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
	addItem(m, @"Open Location...", @selector(openLocation:), @"l", app);
	addItem(m, @"Close Window", @selector(performClose:), @"w", nil);
	m = addSubmenu(bar, @"Edit");           /* target nil: the first responder (the address field) */
	addItem(m, @"Cut", @selector(cut:), @"x", nil);
	addItem(m, @"Copy", @selector(copy:), @"c", nil);
	addItem(m, @"Paste", @selector(paste:), @"v", nil);
	addItem(m, @"Select All", @selector(selectAll:), @"a", nil);
	m = addSubmenu(bar, @"View");
	addItem(m, @"Reload", @selector(reload:), @"r", app);
	addItem(m, @"Back", @selector(goBack:), @"[", app);
	addItem(m, @"Forward", @selector(goForward:), @"]", app);
	[NSApp setMainMenu:bar];
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
	NSString *start = nil;
	const char *home = getenv("HOME");
	char data[1024], cache[1024];
	int i;

	for (i = 1; i < argc; i++)
		if (argv[i][0] != '-') {
			start = FloURLFromInput([NSString stringWithUTF8String:argv[i]]);
			break;
		}
#ifdef GNUSTEP
	/* no NSApplicationMain here: tell gnustep-base the arguments and environment ourselves
	 * (Darwin has no /proc/self to find them from) */
	{ extern char **environ; GSInitializeProcess(argc, argv, environ); }
#endif
	[NSApplication sharedApplication];
	snprintf(data, sizeof data, "%s/.florence/data", home != NULL ? home : "/tmp");
	snprintf(cache, sizeof cache, "%s/.florence/cache", home != NULL ? home : "/tmp");
	make_dirs(data);
	make_dirs(cache);
	if (flo_engine_init(data, cache) != 0)
		return 1;
	FloApp *app = [[FloApp alloc] initWithStartAddress:start];
	[NSApp setDelegate:app];
	buildMenus(app);
	[NSApp run];
	[pool release];
	return 0;
}
