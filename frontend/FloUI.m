/*
 * Florence: the GNUstep user interface. One FloWindow per NetSurf window (toolbar
 * of plain buttons and a URL field, the scrolling page view, a status line), the
 * pump that runs NetSurf's scheduler, the menus and main().
 *
 * Built for little machines: the pump is a one-shot timer that sleeps exactly
 * until the next scheduled NetSurf callback (nothing runs while the page is
 * idle), resizes are debounced so a window drag reflows the page once, and the
 * scroll view blits already-painted pixels instead of repainting them.
 * GPL-2.0-only (see gs.h).
 */
#import <AppKit/AppKit.h>
#include <string.h>
#include <stdlib.h>
#import "FloPage.h"
#include "gnustep/gs.h"

#define BAR_H 30.0
#define STATUS_H 18.0
#define RESIZE_DELAY 0.08

static NSMutableArray *windows;         /* every live FloWindow */
static NSString *startURL;

/* ---- the scheduler pump -------------------------------------------------- */

@interface FloPump : NSObject {
	NSTimer *timer;
	BOOL inTick;
}
+ (FloPump *)shared;
- (void)wake;
@end

@implementation FloPump

+ (FloPump *)shared
{
	static FloPump *p;
	if (p == nil)
		p = [[FloPump alloc] init];
	return p;
}

- (void)arm:(NSTimeInterval)sec
{
	[timer invalidate];
	[timer release];
	timer = [[NSTimer timerWithTimeInterval:sec target:self selector:@selector(tick:)
		userInfo:nil repeats:NO] retain];
	NSRunLoop *rl = [NSRunLoop currentRunLoop];
	[rl addTimer:timer forMode:NSDefaultRunLoopMode];
	[rl addTimer:timer forMode:NSEventTrackingRunLoopMode];   /* keep loading during menus/drags */
}

- (void)tick:(NSTimer *)t
{
	int ms;
	inTick = YES;
	ms = flo_schedule_run();
	inTick = NO;
	/* sleep until the next callback; the floor stops a 1 ms poll loop from eating a slow CPU */
	[self arm:ms < 0 ? 0.25 : (ms < 4 ? 0.004 : (ms > 250 ? 0.25 : ms / 1000.0))];
}

- (void)wake
{
	if (inTick)
		return;                                 /* tick re-arms itself when it returns */
	if (timer != nil && [timer isValid] && [[timer fireDate] timeIntervalSinceNow] < 0.02)
		return;                                 /* already due soon */
	[self arm:0.001];
}

@end

void flo_ui_wake(void) { [[FloPump shared] wake]; }
void flo_ui_quit(void) { [NSApp terminate:nil]; }

/* ---- URL bar input -------------------------------------------------------- */

static NSString *urlFromInput(NSString *in)
{
	NSString *s = [in stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
	if ([s length] == 0)
		return nil;
	if ([s rangeOfString:@"://"].location != NSNotFound || [s hasPrefix:@"about:"] ||
	    [s hasPrefix:@"file:"] || [s hasPrefix:@"data:"])
		return s;
	if ([s rangeOfString:@" "].location == NSNotFound &&
	    ([s rangeOfString:@"."].location != NSNotFound || [s hasPrefix:@"localhost"]))
		return [@"http://" stringByAppendingString:s];
	/* not an address: search (DuckDuckGo's HTML page is the light one) */
	NSMutableCharacterSet *ok = [[[NSCharacterSet alphanumericCharacterSet] mutableCopy] autorelease];
	[ok addCharactersInString:@"-._~"];
	return [@"https://html.duckduckgo.com/html/?q=" stringByAppendingString:
		[s stringByAddingPercentEncodingWithAllowedCharacters:ok]];
}

/* ---- one browser window --------------------------------------------------- */

@interface FloWindow : NSObject <NSWindowDelegate> {
@public
	struct gui_window *gw;
	NSWindow *win;
	NSScrollView *scroll;
	FloPage *page;
	NSTextField *urlField, *status;
	NSButton *backBtn, *fwdBtn, *reloadBtn;
	BOOL loading, userClosed;
}
- (id)initWithGuiWindow:(struct gui_window *)g;
- (void)teardown;
- (void)updateExtent;
- (void)updateButtons;
- (void)focusLocation;
@end

@implementation FloWindow

static NSButton *makeButton(NSString *title, CGFloat x, CGFloat w, id target, SEL action, NSView *in, CGFloat top)
{
	NSButton *b = [[[NSButton alloc] initWithFrame:NSMakeRect(x, top - BAR_H + 3, w, BAR_H - 6)] autorelease];
	[b setTitle:title];
	[b setTarget:target];
	[b setAction:action];
	[b setBezelStyle:NSRoundedBezelStyle];
	[b setAutoresizingMask:NSViewMinYMargin];
	[in addSubview:b];
	return b;
}

- (id)initWithGuiWindow:(struct gui_window *)g
{
	if ((self = [super init]) == nil)
		return nil;
	gw = g;
	g->ui = self;           /* callbacks arrive while the core is still creating the window */
	CGFloat n = (CGFloat)[windows count];
	NSRect frame = NSMakeRect(60 + 24 * fmod(n, 8), 60 + 24 * fmod(n, 8), 960, 640);
	win = [[NSWindow alloc] initWithContentRect:frame
		styleMask:NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask | NSResizableWindowMask
		backing:NSBackingStoreBuffered defer:NO];
	[win setReleasedWhenClosed:NO];
	[win setDelegate:self];
	[win setTitle:@"Florence"];
	[win setAcceptsMouseMovedEvents:YES];
	[win setMinSize:NSMakeSize(320, 200)];

	NSView *cv = [win contentView];
	NSRect b = [cv bounds];
	CGFloat top = NSMaxY(b);
	backBtn = makeButton(@"Back", 4, 52, self, @selector(goBack:), cv, top);
	fwdBtn = makeButton(@"Fwd", 58, 46, self, @selector(goForward:), cv, top);
	reloadBtn = makeButton(@"Reload", 106, 64, self, @selector(reloadOrStop:), cv, top);

	urlField = [[[NSTextField alloc] initWithFrame:NSMakeRect(176, top - BAR_H + 4, b.size.width - 182, BAR_H - 8)] autorelease];
	[urlField setTarget:self];
	[urlField setAction:@selector(go:)];
	[urlField setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
	[cv addSubview:urlField];

	status = [[[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, b.size.width, STATUS_H)] autorelease];
	[status setEditable:NO];
	[status setSelectable:NO];
	[status setBezeled:NO];
	[status setDrawsBackground:NO];
	[status setFont:[NSFont systemFontOfSize:11]];
	[status setAutoresizingMask:NSViewWidthSizable | NSViewMaxYMargin];
	[cv addSubview:status];

	scroll = [[[NSScrollView alloc] initWithFrame:NSMakeRect(0, STATUS_H, b.size.width, b.size.height - BAR_H - STATUS_H)] autorelease];
	[scroll setHasVerticalScroller:YES];
	[scroll setHasHorizontalScroller:YES];
	[scroll setBorderType:NSNoBorder];
	[scroll setDrawsBackground:YES];
	[scroll setBackgroundColor:[NSColor whiteColor]];
	[scroll setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
	[[scroll contentView] setCopiesOnScroll:YES];           /* scrolling moves pixels, repaints the strip */
	[[scroll contentView] setPostsFrameChangedNotifications:YES];
	page = [[FloPage alloc] initWithGuiWindow:g];
	[scroll setDocumentView:page];
	[cv addSubview:scroll];
	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(viewportChanged:)
		name:NSViewFrameDidChangeNotification object:[scroll contentView]];

	[windows addObject:self];
	/* Not shown yet: showing it runs delegate callbacks that ask the core about history, and the
	 * core has not finished building this browser window while it is calling us. Next loop pass. */
	[self performSelector:@selector(showWindow) withObject:nil afterDelay:0];
	return self;
}

- (void)showWindow
{
	if (gw == NULL)
		return;
	flo_trace("ui: show window");
	[self updateButtons];
	[win makeKeyAndOrderFront:nil];
	[win makeFirstResponder:page];
}

- (void)dealloc
{
	[[NSNotificationCenter defaultCenter] removeObserver:self];
	[NSObject cancelPreviousPerformRequestsWithTarget:self];
	[page release];
	[win release];
	[super dealloc];
}

- (void)teardown        /* the core has destroyed the window */
{
	[NSObject cancelPreviousPerformRequestsWithTarget:self];
	[page detach];
	gw = NULL;
	[win setDelegate:nil];
	if (!userClosed)
		[win close];
	[[self retain] autorelease];
	[windows removeObject:self];
	if ([windows count] == 0)
		[NSApp terminate:nil];
}

- (void)windowWillClose:(NSNotification *)n
{
	struct gui_window *g = gw;
	if (g == NULL)
		return;
	userClosed = YES;
	[[self retain] autorelease];
	flo_win_close(g);       /* the core calls back flo_ui_window_free -> teardown */
}

- (void)windowDidBecomeKey:(NSNotification *)n { [self updateButtons]; }

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

- (void)updateButtons
{
	if (gw == NULL)
		return;
	[backBtn setEnabled:flo_win_can_back(gw)];
	[fwdBtn setEnabled:flo_win_can_forward(gw)];
	[reloadBtn setTitle:loading ? @"Stop" : @"Reload"];
}

- (void)focusLocation
{
	[win makeFirstResponder:urlField];
	[urlField selectText:nil];
}

- (void)go:(id)sender
{
	NSString *u = urlFromInput([urlField stringValue]);
	if (u != nil && gw != NULL) {
		flo_win_navigate(gw, [u UTF8String]);
		[win makeFirstResponder:page];
	}
}
- (void)goBack:(id)s { if (gw) flo_win_back(gw); }
- (void)goForward:(id)s { if (gw) flo_win_forward(gw); }
- (void)reloadOrStop:(id)s { if (gw) { if (loading) flo_win_stop(gw); else flo_win_reload(gw); } }

@end

/* ---- the flo_ui_* functions the C glue calls ------------------------------- */

#define W(ui) ((FloWindow *)(ui))

void *flo_ui_window_new(struct gui_window *gw)
{
	return [[FloWindow alloc] initWithGuiWindow:gw];   /* owned by the gui_window */
}

void flo_ui_window_free(void *ui)
{
	FloWindow *w = W(ui);
	[w teardown];
	[w release];
}

void flo_ui_invalidate(void *ui, int x0, int y0, int x1, int y1)
{
	if (x1 < 0)
		[W(ui)->page setNeedsDisplay:YES];
	else
		[W(ui)->page invalidatePageRect:NSMakeRect(x0, y0, x1 - x0, y1 - y0)];
}

void flo_ui_get_scroll(void *ui, int *x, int *y)
{
	NSPoint o = [[W(ui)->scroll contentView] bounds].origin;
	*x = (int)o.x;
	*y = (int)o.y;
}

void flo_ui_set_scroll(void *ui, int x, int y)
{
	NSClipView *clip = [W(ui)->scroll contentView];
	[clip scrollToPoint:[clip constrainScrollPoint:NSMakePoint(x, y)]];
	[W(ui)->scroll reflectScrolledClipView:clip];
}

void flo_ui_get_viewport(void *ui, int *w, int *h)
{
	NSSize s = [W(ui)->scroll contentSize];
	*w = (int)s.width;
	*h = (int)s.height;
}

void flo_ui_update_extent(void *ui) { [W(ui) updateExtent]; }

void flo_ui_set_title(void *ui, const char *title)
{
	if (title != NULL)
		[W(ui)->win setTitle:[NSString stringWithUTF8String:title] ?: @"Florence"];
}

void flo_ui_set_url(void *ui, const char *url)
{
	if (url != NULL)
		[W(ui)->urlField setStringValue:[NSString stringWithUTF8String:url] ?: @""];
	[W(ui) updateButtons];
}

void flo_ui_set_status(void *ui, const char *text)
{
	[W(ui)->status setStringValue:text != NULL ? ([NSString stringWithUTF8String:text] ?: @"") : @""];
}

void flo_ui_set_pointer(void *ui, int p) { [W(ui)->page setPointer:p]; }

void flo_ui_throbber(void *ui, bool on)
{
	W(ui)->loading = on;
	[W(ui) updateButtons];
}

void flo_ui_place_caret(void *ui, int x, int y, int height)
{
	[W(ui)->page placeCaret:NSMakeRect(x, y, 1, height)];
}

void flo_ui_remove_caret(void *ui) { [W(ui)->page removeCaret]; }

char *flo_ui_clipboard_get(size_t *len)
{
	NSString *s = [[NSPasteboard generalPasteboard] stringForType:NSStringPboardType];
	const char *u = [s UTF8String];
	char *r;
	if (u == NULL)
		return NULL;
	*len = strlen(u);
	r = malloc(*len + 1);
	if (r != NULL)
		memcpy(r, u, *len + 1);
	return r;
}

void flo_ui_clipboard_set(const char *text, size_t len)
{
	NSString *s = [[[NSString alloc] initWithBytes:text length:len encoding:NSUTF8StringEncoding] autorelease];
	NSPasteboard *pb = [NSPasteboard generalPasteboard];
	if (s == nil)
		return;
	[pb declareTypes:[NSArray arrayWithObject:NSStringPboardType] owner:nil];
	[pb setString:s forType:NSStringPboardType];
}

/* ---- application ------------------------------------------------------------ */

@interface FloApp : NSObject
@end

@implementation FloApp

static FloWindow *currentWindow(void)
{
	id d = [[NSApp keyWindow] delegate];
	return [d isKindOfClass:[FloWindow class]] ? d : nil;
}

- (void)applicationDidFinishLaunching:(NSNotification *)n
{
	flo_trace("app: did finish launching");
	flo_open_url(startURL != nil ? [startURL UTF8String] : NULL);
	flo_ui_wake();
}

- (void)applicationWillTerminate:(NSNotification *)n
{
	NSArray *all = [[windows copy] autorelease];
	NSUInteger i;
	for (i = 0; i < [all count]; i++) {
		FloWindow *w = [all objectAtIndex:i];
		if (w->gw != NULL) {
			w->userClosed = YES;
			flo_win_close(w->gw);
		}
	}
	flo_core_fini();
}

- (void)newWindow:(id)s { flo_open_url(NULL); }
- (void)closeWindow:(id)s { [[NSApp keyWindow] performClose:nil]; }
- (void)openLocation:(id)s { [currentWindow() focusLocation]; }
- (void)reload:(id)s { FloWindow *w = currentWindow(); if (w && w->gw) flo_win_reload(w->gw); }
- (void)stopLoading:(id)s { FloWindow *w = currentWindow(); if (w && w->gw) flo_win_stop(w->gw); }
- (void)goBack:(id)s { [currentWindow() goBack:s]; }
- (void)goForward:(id)s { [currentWindow() goForward:s]; }

- (BOOL)validateMenuItem:(NSMenuItem *)item
{
	FloWindow *w = currentWindow();
	SEL a = [item action];
	if (a == @selector(newWindow:) || a == @selector(terminate:))
		return YES;
	if (w == nil)
		return NO;
	if (a == @selector(goBack:))
		return w->gw != NULL && flo_win_can_back(w->gw);
	if (a == @selector(goForward:))
		return w->gw != NULL && flo_win_can_forward(w->gw);
	return YES;
}

@end

static NSMenuItem *addItem(NSMenu *m, NSString *title, SEL a, NSString *key, id target)
{
	NSMenuItem *i = [m addItemWithTitle:title action:a keyEquivalent:key];
	[i setTarget:target];
	return i;
}

static NSMenu *addSubmenu(NSMenu *bar, NSString *title)
{
	NSMenuItem *i = [bar addItemWithTitle:title action:NULL keyEquivalent:@""];
	NSMenu *m = [[[NSMenu alloc] initWithTitle:title] autorelease];
	[bar setSubmenu:m forItem:i];
	return m;
}

static void buildMenus(FloApp *app)
{
	NSMenu *bar = [[[NSMenu alloc] initWithTitle:@"Florence"] autorelease];
	NSMenu *m = addSubmenu(bar, @"Florence");
	addItem(m, @"Quit Florence", @selector(terminate:), @"q", NSApp);

	m = addSubmenu(bar, @"File");
	addItem(m, @"New Window", @selector(newWindow:), @"n", app);
	addItem(m, @"Open Location...", @selector(openLocation:), @"l", app);
	addItem(m, @"Close Window", @selector(closeWindow:), @"w", app);

	m = addSubmenu(bar, @"Edit");           /* target nil: the first responder (URL field or page) */
	addItem(m, @"Cut", @selector(cut:), @"x", nil);
	addItem(m, @"Copy", @selector(copy:), @"c", nil);
	addItem(m, @"Paste", @selector(paste:), @"v", nil);
	addItem(m, @"Select All", @selector(selectAll:), @"a", nil);

	m = addSubmenu(bar, @"Go");
	addItem(m, @"Back", @selector(goBack:), @"[", app);
	addItem(m, @"Forward", @selector(goForward:), @"]", app);
	addItem(m, @"Reload", @selector(reload:), @"r", app);
	addItem(m, @"Stop", @selector(stopLoading:), @".", app);

	[NSApp setMainMenu:bar];
}

int main(int argc, char **argv)
{
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
	FloApp *app;
	int i;

	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-') {
			startURL = [urlFromInput([NSString stringWithUTF8String:argv[i]]) retain];
			break;
		}
	}
#ifdef GNUSTEP
	/* no NSApplicationMain here: tell gnustep-base the arguments and environment ourselves
	 * (Darwin has no /proc/self to find them from) */
	{ extern char **environ; GSInitializeProcess(argc, argv, environ); }
#endif
	windows = [[NSMutableArray alloc] init];
	flo_trace("main: gnustep initialised");
	[NSApplication sharedApplication];
	flo_trace("main: NSApplication");
	if (flo_core_init(argc, argv) != 0)
		return 1;
	flo_trace("main: core ready, building menus");
	app = [[FloApp alloc] init];
	[NSApp setDelegate:app];
	buildMenus(app);
	flo_trace("main: run loop");
	[NSApp run];
	[pool release];
	return 0;
}
