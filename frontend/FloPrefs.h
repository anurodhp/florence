/* Florence: the Preferences window. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only */
#import <AppKit/AppKit.h>

/* search engines: the address bar searches with the chosen one (preference "search") */
NSArray *FloSearchEngineNames(void);
NSString *FloSearchURLPrefix(void);             /* the chosen engine's URL up to the query text */

@interface FloPrefs : NSObject <NSWindowDelegate, NSTextFieldDelegate> {
	NSWindow *win;
	NSButton *dntBox, *refBox, *adsBox, *jsBox, *animBox;
	NSPopUpButton *newWinPop, *enginePop, *fontPop, *cachePop;
	NSTextField *homeField, *dlField, *blockInfo, *privacyNote;
}
+ (FloPrefs *)shared;
- (void)show;
@end
