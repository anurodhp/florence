/* Florence: the About window, laid out like a Mac app's: icon, name, version, credits, copyright.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT */
#import "FloAbout.h"
#import "FloBrowser.h"
#include "flo.h"

#define ABOUT_W 320.0
#define ABOUT_H 330.0

static NSTextField *line(NSView *in, NSString *text, CGFloat y, CGFloat h, NSFont *font, NSColor *color)
{
	NSTextField *t = [[[NSTextField alloc] initWithFrame:NSMakeRect(20, y, ABOUT_W - 40, h)] autorelease];
	[t setStringValue:text];
	[t setEditable:NO];
	[t setSelectable:NO];
	[t setBezeled:NO];
	[t setDrawsBackground:NO];
	[t setAlignment:NSCenterTextAlignment];
	[t setFont:font];
	[t setTextColor:color];
	[in addSubview:t];
	return t;
}

@implementation FloAbout

+ (FloAbout *)shared
{
	static FloAbout *a;
	if (a == nil)
		a = [[FloAbout alloc] init];
	return a;
}

static NSString *version(void)
{
	NSDictionary *d = [[NSBundle mainBundle] infoDictionary];
	NSString *v = [d objectForKey:@"ApplicationRelease"];
	if (v == nil)
		v = [d objectForKey:@"CFBundleShortVersionString"];
	return v != nil ? v : @"unknown";
}

- (void)build
{
	NSView *v;
	NSImage *icon = nil;
	NSString *path = [[NSBundle mainBundle] pathForResource:@"Florence-about" ofType:@"png"];
	NSImageView *iv;
	NSColor *grey = [NSColor colorWithCalibratedWhite:0.35 alpha:1.0];
	NSFont *small = [NSFont systemFontOfSize:11];
	CGFloat y = ABOUT_H - 24;

	win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, ABOUT_W, ABOUT_H)
		styleMask:NSTitledWindowMask | NSClosableWindowMask backing:NSBackingStoreBuffered defer:NO];
	[win setTitle:@"About Florence"];
	[win setReleasedWhenClosed:NO];
	v = [win contentView];

	if (path != nil)
		icon = [[[NSImage alloc] initWithContentsOfFile:path] autorelease];
	if (icon == nil)
		icon = FloAppIcon();
	y -= 128;
	iv = [[[NSImageView alloc] initWithFrame:NSMakeRect((ABOUT_W - 128) / 2, y, 128, 128)] autorelease];
	[iv setImage:icon];
	[iv setImageScaling:NSScaleProportionally];
	[v addSubview:iv];

	y -= 34;
	line(v, @"Florence", y, 26, [NSFont boldSystemFontOfSize:20], [NSColor blackColor]);
	y -= 20;
	line(v, [NSString stringWithFormat:@"Version %@", version()], y, 16, [NSFont systemFontOfSize:12], grey);
	y -= 28;
	line(v, @"Web engine: WPE WebKit 2.54", y, 16, small, grey);
	y -= 30;
	line(v, @"Copyright © 2026 Anurodh Pokharel", y, 16, small, [NSColor blackColor]);
	y -= 17;
	line(v, @"Released under the MIT licence.", y, 16, small, grey);
	y -= 17;
	line(v, @"Includes WebKit, © Apple Inc. and others (LGPL, BSD).", y, 16, small, grey);
}

- (void)show
{
	if (win == nil)
		[self build];
	[win center];
	[win makeKeyAndOrderFront:nil];
}

@end
