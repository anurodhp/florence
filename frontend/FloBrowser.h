/*
 * Florence: a browser window: toolbar, tab strip (shown only with two or more tabs),
 * the current tab's page and a status line. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only
 */
#import <AppKit/AppKit.h>
#import "FloTab.h"
#import "FloToolbar.h"

NSString *FloURLFromInput(NSString *input);     /* address, or a search for anything else */
NSImage *FloAppIcon(void);                      /* defined by FloUI.m */

extern NSString *const FloBookmarksChanged;      /* posted when the bookmark list changes */

@interface FloBrowser : NSObject <NSWindowDelegate> {
@public
	NSWindow *win;
	FloToolbarBand *band;
	FloToolButton *backBtn, *fwdBtn, *starBtn, *newTabBtn;
	FloAddressBar *addr;
	NSTextField *urlField;          /* the address bar's text field */
	FloTabStrip *strip;
	NSView *container;
	FloStatusLabel *status;
	NSMutableArray *tabs;
	FloTab *current;
	BOOL closing, chromePending;
}
+ (NSMutableArray *)all;                /* every live browser window */
+ (FloBrowser *)key;                    /* the key window's browser, if any */
- (void)relayout;
- (void)layoutStrip;
- (void)rebuildStrip;
- (void)refreshChrome;
- (void)showWindow;
- (void)addTab:(FloTab *)t select:(BOOL)select;
- (void)removeTab:(FloTab *)t;
- (void)selectTab:(FloTab *)t;
- (void)setNeedsChrome;                 /* title/address/buttons changed: refresh next loop pass */
- (void)setStatusText:(NSString *)s;    /* the hover label; hides itself after a few seconds */
- (void)focusLocation;
- (void)closeCurrentTab;
- (void)nextTab:(int)delta;
- (void)newTab;
- (void)toggleBookmark;
- (void)goBack:(id)sender;
- (void)goForward:(id)sender;
- (void)reloadOrStop:(id)sender;
@end
