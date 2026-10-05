/*
 * Florence: a browser window, the bare minimum: back, forward, reload/stop, an address field, the
 * page, and a status line. One page per window; tabs, bookmarks, history and preferences are not
 * here yet (the earlier UI is on `master`: `git show master:frontend/FloBrowser.m`).
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#import <AppKit/AppKit.h>
#import "FloPageView.h"

NSString *FloURLFromInput(NSString *input);     /* an address, or a web search for anything else */

@interface FloWindow : NSObject <NSWindowDelegate> {
	NSWindow *win;
	NSButton *backBtn, *fwdBtn, *reloadBtn;
	NSTextField *addr, *status;
	FloPageView *view;
	struct flo_page *page;
}
+ (NSMutableArray *)all;
+ (FloWindow *)key;                    /* the window that has the keyboard, else the newest */
- (id)initWithAddress:(NSString *)url;
- (void)focusAddress;
- (void)goBack:(id)s;
- (void)goForward:(id)s;
- (void)reloadOrStop:(id)s;
@end
