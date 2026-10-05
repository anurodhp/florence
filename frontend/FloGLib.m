/* Florence: GLib's main context pumped from GNUstep's run loop. See FloGLib.h. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT */
#import <AppKit/AppKit.h>
#import "FloGLib.h"
#include <stdint.h>
#include "flo.h"

/* Both modes: pages keep loading while a menu is open or a window is being dragged. */
#define FLO_MODES() (NSArray *)[NSArray arrayWithObjects:NSDefaultRunLoopMode, NSEventTrackingRunLoopMode, nil]

@implementation FloGLib

+ (FloGLib *)shared
{
	static FloGLib *g;
	if (g == nil)
		g = [[FloGLib alloc] init];
	return g;
}

- (void)start { [self arm]; }

- (void)disarm
{
	NSRunLoop *rl = [NSRunLoop currentRunLoop];
	NSEnumerator *e;
	NSString *mode;
	int i;

	for (i = 0; i < armedCount; i++) {
		e = [FLO_MODES() objectEnumerator];
		while ((mode = [e nextObject]) != nil)
			[rl removeEvent:(void *)(intptr_t)armedFds[i] type:(RunLoopEventType)armedTypes[i] forMode:mode all:NO];
	}
	armedCount = 0;
	[timer invalidate];
	[timer release];
	timer = nil;
}

/* Ask GLib what it is waiting for and wait for exactly that, in the run loop. */
- (void)arm
{
	struct flo_glib_wait w;
	NSRunLoop *rl = [NSRunLoop currentRunLoop];
	NSEnumerator *e;
	NSString *mode;
	int i;

	if (!flo_glib_prepare(&w))
		return;
	for (i = 0; i < w.n && armedCount < 62; i++) {
		int t;
		for (t = 0; t < 2; t++) {
			RunLoopEventType type = t == 0 ? ET_RDESC : ET_WDESC;
			if (t == 0 ? !w.read[i] : !w.write[i])
				continue;
			armedFds[armedCount] = w.fd[i];
			armedTypes[armedCount++] = (int)type;
			e = [FLO_MODES() objectEnumerator];
			while ((mode = [e nextObject]) != nil)
				[rl addEvent:(void *)(intptr_t)w.fd[i] type:type watcher:self forMode:mode];
		}
	}
	if (w.timeout_ms >= 0) {
		timer = [[NSTimer timerWithTimeInterval:w.timeout_ms > 0 ? w.timeout_ms / 1000.0 : 0.001
			target:self selector:@selector(timerFired:) userInfo:nil repeats:NO] retain];
		e = [FLO_MODES() objectEnumerator];
		while ((mode = [e nextObject]) != nil)
			[rl addTimer:timer forMode:mode];
	}
}

- (void)fire
{
	if (firing)
		return;                         /* a dispatch that spins the run loop (a modal panel) */
	firing = YES;
	[self disarm];
	flo_glib_dispatch();
	firing = NO;
	[self arm];
}

- (void)timerFired:(NSTimer *)t { [self fire]; }

/* RunLoopEvents: one of the watched descriptors is ready */
- (void)receivedEvent:(void *)data type:(RunLoopEventType)type extra:(void *)extra forMode:(NSString *)mode
{
	[self fire];
}

@end
