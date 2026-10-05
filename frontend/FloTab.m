/* Florence: a browser tab. See FloTab.h. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT */
#import "FloTab.h"
#import "FloBrowser.h"
#import "FloStore.h"

#define T(ui) ((FloTab *)(ui))

static NSString *str(const char *s) { return s != NULL ? [NSString stringWithUTF8String:s] : nil; }

static void recordHistory(FloTab *tab)
{
	if ([tab->url length] > 0 && ![tab->url hasPrefix:@"about:"]) {
		[[FloStore history] add:tab->url title:[tab displayTitle]];
		[[NSNotificationCenter defaultCenter] postNotificationName:FloHistoryChanged object:nil];
	}
}

static void ev_changed(void *ui, int x, int y, int w, int h) { [T(ui)->page frameChangedX:x y:y w:w h:h]; }
static void ev_progress(void *ui, double p) { (void)ui; (void)p; }

static void ev_title(void *ui, const char *t)
{
	FloTab *tab = T(ui);
	[tab setTitle:str(t)];
	[tab->browser setNeedsChrome];
	if (!tab->loading)              /* the title can arrive after the load finished: the history entry gets it */
		recordHistory(tab);
}

static void ev_uri(void *ui, const char *u)
{
	FloTab *tab = T(ui);
	NSString *s = str(u);
	/* a blank page shows an empty address bar */
	[tab setUrl:[s isEqualToString:@"about:blank"] ? @"" : s];
	[tab->browser setNeedsChrome];
}

static void ev_nav(void *ui, bool back, bool fwd)
{
	FloTab *tab = T(ui);
	tab->canBack = back;
	tab->canForward = fwd;
	[tab->browser setNeedsChrome];
}

static void ev_hover(void *ui, const char *link)
{
	FloTab *tab = T(ui);
	[tab setStatus:str(link)];
	if (tab->browser != nil && tab->browser->current == tab)
		[tab->browser setStatusText:tab->status];
}

static void ev_loading(void *ui, bool on)
{
	FloTab *tab = T(ui);
	tab->loading = on;
	[tab->browser setNeedsChrome];
	if (!on)                        /* the page is done: it belongs in the history */
		recordHistory(tab);
}

static void ev_found(void *ui, bool found)
{
	FloTab *tab = T(ui);
	if (tab->browser != nil && tab->browser->current == tab)
		[tab->browser setFindFound:found];
}

static void ev_open_tab(void *ui, const char *uri)
{
	FloTab *tab = T(ui);
	if (tab->browser != nil && uri != NULL)
		[tab->browser openTabWithAddress:str(uri) select:NO];
}

static const struct flo_page_events events = { ev_changed, ev_title, ev_uri, ev_progress, ev_nav, ev_hover,
					       ev_loading, ev_found, ev_open_tab };

@implementation FloTab

- (id)initWithAddress:(NSString *)address
{
	if ((self = [super init]) == nil)
		return nil;
	title = [@"" retain];
	url = [@"" retain];
	status = [@"" retain];
	page = [[FloPageView alloc] initWithFrame:NSMakeRect(0, 0, 800, 560)];
	[page setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
	fp = flo_page_new(&events, self, 800, 560);     /* callbacks may arrive from here on */
	[page setPage:fp];
	if (address != nil)
		flo_page_load(fp, [address UTF8String]);
	return self;
}

- (void)dealloc
{
	[self teardown];
	[title release];
	[url release];
	[status release];
	[page release];
	[super dealloc];
}

- (void)teardown
{
	if (fp == NULL)
		return;
	[page setPage:NULL];
	flo_page_free(fp);
	fp = NULL;
	[page removeFromSuperview];
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

@end
