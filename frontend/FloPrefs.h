/* Florence: the Preferences window. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT */
#import <AppKit/AppKit.h>

/* search engines: the address bar searches with the chosen one (preference "search") */
NSArray *FloSearchEngineNames(void);
NSString *FloSearchURLPrefix(void);             /* the chosen engine's URL up to the query text */

@interface FloPrefs : NSObject <NSWindowDelegate, NSTextFieldDelegate> {
	NSWindow *win;
	NSButton *adsBox;
	NSPopUpButton *enginePop, *fontPop;
	NSTextField *homeField, *dlField, *privacyNote;
}
+ (FloPrefs *)shared;
- (void)show;
@end
