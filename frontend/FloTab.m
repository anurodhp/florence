/* Florence: a browser tab. See FloTab.h. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT */
#import "FloTab.h"

#define RESIZE_DELAY 0.08

@implementation FloTab

- (id)initWithGuiWindow:(struct gui_window *)g
{
	if ((self = [super init]) == nil)
		return nil;
	gw = g;
	g->ui = self;           /* callbacks arrive while the core is still creating the window */
	title = [@"" retain];
	url = [@"" retain];
	status = [@"" retain];

	scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 800, 560)];
	[scroll setHasVerticalScroller:YES];
	[scroll setHasHorizontalScroller:YES];
	[scroll setBorderType:NSNoBorder];
	[scroll setDrawsBackground:YES];
	[scroll setBackgroundColor:[NSColor whiteColor]];
	[[scroll contentView] setCopiesOnScroll:YES];           /* scrolling moves pixels, repaints the strip */
	[[scroll contentView] setPostsFrameChangedNotifications:YES];
	page = [[FloPage alloc] initWithGuiWindow:g];
	[scroll setDocumentView:page];
	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(viewportChanged:)
		name:NSViewFrameDidChangeNotification object:[scroll contentView]];
	return self;
}

- (void)dealloc
{
	[[NSNotificationCenter defaultCenter] removeObserver:self];
	[NSObject cancelPreviousPerformRequestsWithTarget:self];
	[title release];
	[url release];
	[status release];
	[page release];
	[scroll release];
	[super dealloc];
}

- (void)teardown
{
	[NSObject cancelPreviousPerformRequestsWithTarget:self];
	[[NSNotificationCenter defaultCenter] removeObserver:self];
	[page detach];
	gw = NULL;
	[scroll removeFromSuperview];
}

#define SETTER(name, ivar) \
- (void)name:(NSString *)v { if (v == nil) v = @""; if (![ivar isEqualToString:v]) { [ivar release]; ivar = [v copy]; } }
SETTER(setTitle, title)
SETTER(setUrl, url)
SETTER(setStatus, status)

- (NSString *)displayTitle
{
	if ([title length] > 0)
		return title;
	return [url length] > 0 ? url : @"New Tab";
}

/* viewport changes: reflow once, shortly after the last change */
- (void)viewportChanged:(NSNotification *)n
{
	[NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(applyResize) object:nil];
	[self performSelector:@selector(applyResize) withObject:nil afterDelay:RESIZE_DELAY];
}

- (void)applyResize
{
	NSSize s = [scroll contentSize];
	if (gw == NULL)
		return;
	flo_win_resize(gw, (int)s.width, (int)s.height);
	[self updateExtent];
}

- (void)updateExtent
{
	int w, h;
	NSSize vs = [scroll contentSize];
	if (gw == NULL)
		return;
	flo_win_extent(gw, &w, &h);
	NSSize ns = NSMakeSize(w > vs.width ? w : vs.width, h > vs.height ? h : vs.height);
	if (!NSEqualSizes(ns, [page frame].size))
		[page setFrameSize:ns];
}

@end
