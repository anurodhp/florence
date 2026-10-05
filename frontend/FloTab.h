/*
 * Florence: one browser tab = one engine page (a WebKit view). Owns the page view and the per-tab title,
 * address, status and load state, which the engine reports through the flo_page_events callbacks.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#import <AppKit/AppKit.h>
#import "FloPageView.h"
#include "flo.h"

@class FloBrowser;

@interface FloTab : NSObject {
@public
	struct flo_page *fp;
	FloBrowser *browser;            /* the window it is in; not retained */
	FloPageView *page;
	NSString *title, *url, *status;
	BOOL loading, canBack, canForward;
}
- (id)initWithAddress:(NSString *)address;      /* loads it when not nil */
- (void)teardown;                       /* free the engine page; the tab is finished */
- (NSString *)displayTitle;             /* title, else address, else "New Tab" */
- (NSString *)hoverLink;                /* the link under the pointer, or nil */
- (void)openHoverLinkInBackground;
- (NSMenu *)contextMenu;                /* for what is under the pointer; nil if the tab is gone */
@end
