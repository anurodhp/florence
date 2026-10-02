/*
 * Florence: one browser tab = one NetSurf gui_window. Owns the page view and its
 * scroll view and the per-tab title, address and status. GPL-2.0-only (see gs.h).
 */
#import <AppKit/AppKit.h>
#import "FloPage.h"
#include "gnustep/gs.h"

@class FloBrowser;

@interface FloTab : NSObject {
@public
	struct gui_window *gw;
	FloBrowser *browser;            /* the window it is in; not retained */
	NSScrollView *scroll;
	FloPage *page;
	NSString *title, *url, *status;
	BOOL loading;
}
- (id)initWithGuiWindow:(struct gui_window *)g;
- (void)teardown;                       /* the core has destroyed the window */
- (void)setTitle:(NSString *)t;
- (void)setUrl:(NSString *)u;
- (void)setStatus:(NSString *)s;
- (NSString *)displayTitle;             /* title, else address, else "New Tab" */
- (void)updateExtent;
- (void)applyResize;
@end
