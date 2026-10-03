/*
 * Florence: drives GLib's main context from GNUstep's run loop, so WebKit's UI-process half runs
 * inside the one run loop the application already has (no thread, no polling timer). See flo.h.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only
 */
#import <Foundation/Foundation.h>

@interface FloGLib : NSObject <RunLoopEvents> {
	NSTimer *timer;
	int armedFds[64];               /* descriptors currently registered with the run loop, with the event type */
	int armedTypes[64];
	int armedCount;
	BOOL firing;
}
+ (FloGLib *)shared;
- (void)start;                          /* after flo_engine_init: begin pumping */
- (void)arm;
- (void)disarm;
- (void)fire;
@end
