/*
 * Florence: how fast can GNUstep put a 960x600 frame on the screen? Isolates the UI's draw path from WebKit: a view draws a
 * synthetic BGRA buffer (what the engine hands over) in several ways and reports the time per frame, so the ways can be
 * compared, and so "slow in Florence" can be told from "slow in GNUstep" (mode 0 is a plain fill of the same area).
 *   draw_bench [frames]         run on the Pi under the X server: DISPLAY=:0 XAUTHORITY=... draw_bench
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#import <AppKit/AppKit.h>
#import "FloCairo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W 960
#define H 600

static unsigned char *bgra;             /* the "engine buffer": BGRA, rows top-down, red top-left, blue bottom-left */
static unsigned char *rgbPersist;       /* mode 2's persistent RGB copy */
static NSBitmapImageRep *persistRep;
static NSBitmapImageRep *bgraRep;       /* mode 3 */
static int mode;
static NSRect dirtyBench;

static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void makeFrame(void)
{
	int x, y;
	bgra = malloc((size_t)W * H * 4);
	for (y = 0; y < H; y++)
		for (x = 0; x < W; x++) {
			unsigned char *p = bgra + ((size_t)y * W + x) * 4;
			unsigned char b = (unsigned char)(x * 255 / W), g = (unsigned char)(y * 255 / H), r = (unsigned char)((x ^ y) & 255);
			if (x < 100 && y < 100) { b = 0; g = 0; r = 255; }                 /* red top-left */
			else if (x < 100 && y >= H - 100) { b = 255; g = 0; r = 0; }       /* blue bottom-left */
			p[0] = b; p[1] = g; p[2] = r; p[3] = 255;
		}
}

static void toRGB(unsigned char *dst, int x0, int y0, int w, int h)
{
	int y;
	for (y = 0; y < h; y++) {
		const unsigned char *s = bgra + ((size_t)(y0 + y) * W + x0) * 4;
		unsigned char *d = dst + ((size_t)(y0 + y) * W + x0) * 3;
		int x;
		for (x = 0; x < w; x++, s += 4, d += 3) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; }
	}
}

@interface BenchView : NSView
@end

@implementation BenchView
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }

- (void)drawRect:(NSRect)dirty
{
	NSRect r = NSIntersectionRect(dirty, NSMakeRect(0, 0, W, H));
	int x0 = (int)r.origin.x, y0 = (int)r.origin.y, w = (int)r.size.width, h = (int)r.size.height;

	if (mode == 0) {
		[[NSColor whiteColor] set];
		NSRectFill(dirty);
		return;
	}
	if (mode == 1) {                        /* Florence today: convert the rect, new rep and image, mirrored draw */
		static unsigned char *buf; static size_t cap;
		unsigned char *planes[1];
		NSBitmapImageRep *rep; NSImage *img; NSAffineTransform *t;
		int y;
		if ((size_t)w * h * 3 > cap) { buf = realloc(buf, (size_t)w * h * 3); cap = (size_t)w * h * 3; }
		for (y = 0; y < h; y++) {
			const unsigned char *s = bgra + ((size_t)(y0 + y) * W + x0) * 4;
			unsigned char *d = buf + (size_t)y * w * 3;
			int x;
			for (x = 0; x < w; x++, s += 4, d += 3) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; }
		}
		planes[0] = buf;
		rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:planes pixelsWide:w pixelsHigh:h bitsPerSample:8 samplesPerPixel:3
			hasAlpha:NO isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:w * 3 bitsPerPixel:24];
		img = [[NSImage alloc] initWithSize:NSMakeSize(w, h)];
		[img addRepresentation:rep];
		[NSGraphicsContext saveGraphicsState];
		t = [NSAffineTransform transform];
		[t translateXBy:x0 yBy:y0 + h];
		[t scaleXBy:1.0 yBy:-1.0];
		[t concat];
		[img drawInRect:NSMakeRect(0, 0, w, h) fromRect:NSZeroRect operation:NSCompositeCopy fraction:1.0];
		[NSGraphicsContext restoreGraphicsState];
		[img release]; [rep release];
		return;
	}
	if (mode == 2) {                        /* the persistent RGB copy, one rep and image, drawn from a sub-rectangle */
		static NSImage *img;
		NSAffineTransform *t;
		if (img == nil) { img = [[NSImage alloc] initWithSize:NSMakeSize(W, H)]; [img addRepresentation:persistRep]; }
		[NSGraphicsContext saveGraphicsState];
		t = [NSAffineTransform transform];
		[t translateXBy:0 yBy:H];
		[t scaleXBy:1.0 yBy:-1.0];
		[t concat];
		[img drawInRect:NSMakeRect(x0, H - y0 - h, w, h) fromRect:NSMakeRect(x0, H - y0 - h, w, h) operation:NSCompositeCopy fraction:1.0];
		[NSGraphicsContext restoreGraphicsState];
		return;
	}
	if (mode == 4) {                        /* a fresh rep and image around the persistent RGB buffer: nothing for GNUstep to cache */
		unsigned char *planes[1] = { rgbPersist + ((size_t)y0 * W + x0) * 3 };
		NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:planes pixelsWide:w pixelsHigh:h bitsPerSample:8
			samplesPerPixel:3 hasAlpha:NO isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:W * 3 bitsPerPixel:24];
		NSImage *img = [[NSImage alloc] initWithSize:NSMakeSize(w, h)];
		NSAffineTransform *t;
		[img addRepresentation:rep];
		[NSGraphicsContext saveGraphicsState];
		t = [NSAffineTransform transform];
		[t translateXBy:x0 yBy:y0 + h];
		[t scaleXBy:1.0 yBy:-1.0];
		[t concat];
		[img drawInRect:NSMakeRect(0, 0, w, h) fromRect:NSZeroRect operation:NSCompositeCopy fraction:1.0];
		[NSGraphicsContext restoreGraphicsState];
		[img release]; [rep release];
		return;
	}
	if (mode == 5) {                        /* the engine buffer through cairo directly: no conversion, no image object */
		if (!FloCairoDraw(bgra, W * 4, W, H, r)) {
			[[NSColor magentaColor] set];
			NSRectFill(dirty);
		}
		return;
	}
	if (mode == 3) {                        /* the engine's BGRA used as is: a 32-bit little-endian rep, no conversion */
		static NSImage *img;
		NSAffineTransform *t;
		if (img == nil) { img = [[NSImage alloc] initWithSize:NSMakeSize(W, H)]; [img addRepresentation:bgraRep]; }
		[NSGraphicsContext saveGraphicsState];
		t = [NSAffineTransform transform];
		[t translateXBy:0 yBy:H];
		[t scaleXBy:1.0 yBy:-1.0];
		[t concat];
		[img drawInRect:NSMakeRect(x0, H - y0 - h, w, h) fromRect:NSMakeRect(x0, H - y0 - h, w, h) operation:NSCompositeCopy fraction:1.0];
		[NSGraphicsContext restoreGraphicsState];
	}
}
@end

@interface Bench : NSObject {
@public
	NSWindow *win;
	BenchView *view;
	int frames;
}
@end

@implementation Bench

/* read back what a mode drew and check the orientation and colours: red top-left, blue bottom-left */
- (NSString *)check
{
	NSBitmapImageRep *rep = [view bitmapImageRepForCachingDisplayInRect:[view bounds]];
	NSColor *tl, *bl;
	[view cacheDisplayInRect:[view bounds] toBitmapImageRep:rep];
	tl = [rep colorAtX:20 y:20];
	bl = [rep colorAtX:20 y:H - 20];
	return [NSString stringWithFormat:@"top-left r%.0f g%.0f b%.0f, bottom-left r%.0f g%.0f b%.0f",
		[tl redComponent] * 255, [tl greenComponent] * 255, [tl blueComponent] * 255,
		[bl redComponent] * 255, [bl greenComponent] * 255, [bl blueComponent] * 255];
}

- (void)runMode:(int)m name:(const char *)name rect:(NSRect)r
{
	double t0;
	int i;
	mode = m;
	[view display];                         /* warm up: first draw of a mode */
	t0 = now();
	for (i = 0; i < frames; i++) {
		/* the engine changed something: a 40x40 square of the source changes colour every frame, so a cached copy would be stale */
		int sq = i % 2 ? 200 : 20, cx, cy;
		for (cy = 300; cy < 340; cy++)
			for (cx = 400; cx < 440; cx++) { unsigned char *p = bgra + ((size_t)cy * W + cx) * 4; p[0] = (unsigned char)sq; p[1] = 0; p[2] = (unsigned char)(255 - sq); }
		toRGB(rgbPersist, 400, 300, 40, 40);
		[view displayRect:r];
		[[view window] flushWindow];
	}
	(void)[NSEvent mouseLocation];          /* a round trip to the X server: it has drawn everything sent so far */
	{
		double ms = (now() - t0) / frames;
		printf("mode %d %-34s %4dx%-4d  %7.1f ms/frame\n", m, name, (int)r.size.width, (int)r.size.height, ms);
	}
	fflush(stdout);
}

- (void)run:(id)unused
{
	NSRect full = NSMakeRect(0, 0, W, H), strip = NSMakeRect(W - 21, 0, 21, H), small = NSMakeRect(300, 200, 200, 100);
	printf("draw_bench: %d frames per line, view %dx%d\n", frames, W, H);
	[self runMode:0 name:"fill white (GNUstep baseline)" rect:full];
	[self runMode:1 name:"today: convert+rep+image per draw" rect:full];
	[self runMode:1 name:"today" rect:strip];
	[self runMode:1 name:"today" rect:small];
	[self runMode:2 name:"persistent RGB rep, sub-rect draw" rect:full];
	[self runMode:2 name:"persistent RGB rep" rect:strip];
	[self runMode:2 name:"persistent RGB rep" rect:small];
	[self runMode:4 name:"fresh rep on persistent RGB buffer" rect:full];
	[self runMode:4 name:"fresh rep on persistent RGB" rect:strip];
	[self runMode:4 name:"fresh rep on persistent RGB" rect:small];
	[self runMode:5 name:"cairo direct, zero-copy" rect:full];
	[self runMode:5 name:"cairo direct" rect:strip];
	[self runMode:5 name:"cairo direct" rect:small];
	{
		double t0 = now(); int k;
		for (k = 0; k < 10; k++) toRGB(rgbPersist, 0, 0, W, H);
		printf("convert 960x600 BGRA->RGB on the CPU alone: %.1f ms\n", (now() - t0) / 10);
	}
	/* freshness: change the source and see whether a later draw shows it */
	for (mode = 4; mode <= 5; mode++) {
		unsigned char *p = bgra + ((size_t)400 * W + 400) * 4;
		[view display];
		p[0] = 0; p[1] = 255; p[2] = 0; toRGB(rgbPersist, 400, 400, 1, 1);          /* now green */
		[view display];
		{
			NSBitmapImageRep *rep = [view bitmapImageRepForCachingDisplayInRect:[view bounds]];
			NSColor *c;
			[view cacheDisplayInRect:[view bounds] toBitmapImageRep:rep];
			c = [rep colorAtX:400 y:400];
			printf("mode %d freshness after changing a pixel to green: r%.0f g%.0f b%.0f\n", mode, [c redComponent] * 255, [c greenComponent] * 255, [c blueComponent] * 255);
		}
		p[0] = 0; p[1] = 0; p[2] = 0; toRGB(rgbPersist, 400, 400, 1, 1);
	}
	for (mode = 1; mode <= 5; mode += (mode == 2 ? 3 : 1)) {
		[view display];
		printf("mode %d pixels: %s\n", mode, [[self check] UTF8String]);
	}
	fflush(stdout);
	[NSApp terminate:nil];
}

- (void)applicationDidFinishLaunching:(NSNotification *)n
{
	int x;
	win = [[NSWindow alloc] initWithContentRect:NSMakeRect(40, 40, W, H) styleMask:NSTitledWindowMask backing:NSBackingStoreBuffered defer:NO];
	view = [[BenchView alloc] initWithFrame:NSMakeRect(0, 0, W, H)];
	[win setContentView:view];
	[win makeKeyAndOrderFront:nil];
	makeFrame();
	rgbPersist = malloc((size_t)W * H * 3);
	toRGB(rgbPersist, 0, 0, W, H);
	{
		unsigned char *planes[1] = { rgbPersist };
		persistRep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:planes pixelsWide:W pixelsHigh:H bitsPerSample:8 samplesPerPixel:3
			hasAlpha:NO isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:W * 3 bitsPerPixel:24];
		unsigned char *planes4[1] = { bgra };
		bgraRep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:planes4 pixelsWide:W pixelsHigh:H bitsPerSample:8 samplesPerPixel:4
			hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
			bitmapFormat:NSAlphaFirstBitmapFormat | NSBitmapFormatThirtyTwoBitLittleEndian bytesPerRow:W * 4 bitsPerPixel:32];
	}
	(void)x;
	[self performSelector:@selector(run:) withObject:nil afterDelay:1.5];     /* after the window is mapped */
}
@end

int main(int argc, char **argv)
{
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
	Bench *b = [[Bench alloc] init];
#ifdef GNUSTEP
	{ extern char **environ; GSInitializeProcess(argc, argv, environ); }
#endif
	[NSApplication sharedApplication];
	b->frames = argc > 1 ? atoi(argv[1]) : 20;
	[NSApp setDelegate:b];
	[NSApp run];
	[pool release];
	return 0;
}
