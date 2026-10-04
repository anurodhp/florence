/*
 * Florence: the page view. A flipped NSView that draws the engine's finished frame (BGRA in shared
 * memory) and sends it mouse, wheel and key input. Only the exposed rectangle is converted and
 * drawn, through one reusable buffer. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#import <AppKit/AppKit.h>

struct flo_page;

@interface FloPageView : NSView {
	struct flo_page *page;          /* not owned */
	unsigned char *rgb;             /* the exposed rectangle as 24-bit RGB, grown never shrunk */
	size_t rgbCap;
	NSSize sentSize;                /* the size the engine was last told */
}
- (void)setPage:(struct flo_page *)p;
- (void)frameChangedX:(int)x y:(int)y w:(int)w h:(int)h;   /* the engine drew: repaint that rectangle */
@end
