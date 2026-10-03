/* Florence: the About window. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only */
#import <AppKit/AppKit.h>

@interface FloAbout : NSObject {
	NSWindow *win;
}
+ (FloAbout *)shared;
- (void)show;
@end
