/*
 * Florence: Safari-like toolbar parts: flat icon
 * buttons (Lucide glyphs from the bundle's Resources, vector fallback), the rounded address field, the toolbar band, the tab strip and the hover-status
 * label. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only
 */
#import <AppKit/AppKit.h>

@class FloBrowser;

typedef enum { FloIconBack, FloIconForward, FloIconReload, FloIconStop, FloIconPlus,
	       FloIconStar, FloIconStarFilled } FloIcon;

/* a flat icon button: no bezel, a soft rounded highlight on hover and press */
@interface FloToolButton : NSButton {
	FloIcon icon;
	BOOL hover;
	NSTrackingRectTag track;
	BOOL tracking;
}
- (void)setIcon:(FloIcon)i;
@end

/* the rounded address field with a lock for https and a reload/stop button inside it */
enum { FloSecurityNone = 0, FloSecurityVerified = 1, FloSecurityOverridden = 2 };   /* matches flo_win_security() */

@interface FloAddressBar : NSView {
	NSTextField *field;
	FloToolButton *reload;
	int security;                   /* FloSecurity */
}
- (id)initWithTarget:(id)target goAction:(SEL)go reloadAction:(SEL)reloadAction;
- (NSTextField *)field;
- (FloToolButton *)reloadButton;
- (void)setSecurity:(int)s;      /* FloSecurity* */
- (void)layoutInside;
@end

/* the flat toolbar background with its hairline underneath */
@interface FloToolbarBand : NSView
@end

/* tabs, Safari style: equal widths, centred titles, a close mark at the left of each */
@interface FloTabStrip : NSView {
	FloBrowser *browser;
}
- (void)setBrowser:(FloBrowser *)b;
@end

/* the small label at the bottom left of the page that shows a link's address while hovering */
@interface FloStatusLabel : NSView {
	NSString *text;
}
- (void)setText:(NSString *)t;
@end
