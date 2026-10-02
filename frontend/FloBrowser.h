/*
 * Florence: a browser window: toolbar, tab strip (shown only with two or more tabs),
 * the current tab's page and a status line. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only
 */
#import <AppKit/AppKit.h>
#import "FloTab.h"

NSString *FloURLFromInput(NSString *input);     /* address, or a search for anything else */
NSImage *FloAppIcon(void);                      /* defined by FloUI.m */

@interface FloBrowser : NSObject <NSWindowDelegate> {
@public
	NSWindow *win;
	NSView *container;
	NSView *strip;
	NSTextField *urlField, *status;
	NSButton *backBtn, *fwdBtn, *reloadBtn, *newTabBtn;
	NSMutableArray *tabs;
	FloTab *current;
	BOOL closing, chromePending;
}
+ (NSMutableArray *)all;                /* every live browser window */
+ (FloBrowser *)key;                    /* the key window's browser, if any */
- (void)addTab:(FloTab *)t select:(BOOL)select;
- (void)removeTab:(FloTab *)t;
- (void)selectTab:(FloTab *)t;
- (void)relayout;
- (void)layoutStrip;
- (void)rebuildStrip;
- (void)refreshChrome;
- (void)showWindow;
- (void)setNeedsChrome;                 /* title/address/buttons changed: refresh next loop pass */
- (void)focusLocation;
- (void)closeCurrentTab;
- (void)nextTab:(int)delta;
- (void)newTab;
- (void)goBack:(id)sender;
- (void)goForward:(id)sender;
- (void)reloadOrStop:(id)sender;
@end
