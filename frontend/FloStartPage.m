/* Florence: the start page. See FloStartPage.h. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT */
#import "FloStartPage.h"
#import "FloStore.h"
#include <stdlib.h>

#define TILES 8                 /* per row */

static NSString *startPath(void)
{
	const char *h = getenv("HOME");
	NSString *home = h != NULL ? [NSString stringWithUTF8String:h] : NSHomeDirectory();
	return [home stringByAppendingPathComponent:@".florence/start/index.html"];
}

BOOL FloIsStartPageURL(NSString *url) { return [url hasPrefix:@"file://"] && [url hasSuffix:@"/.florence/start/index.html"]; }

static NSString *html(NSString *s)
{
	s = [s stringByReplacingOccurrencesOfString:@"&" withString:@"&amp;"];
	s = [s stringByReplacingOccurrencesOfString:@"<" withString:@"&lt;"];
	s = [s stringByReplacingOccurrencesOfString:@">" withString:@"&gt;"];
	return [s stringByReplacingOccurrencesOfString:@"\"" withString:@"&quot;"];
}

/* "https://www.Example.com:8080/x" -> "example.com"; nil when there is no host or it is not http(s) */
static NSString *hostOf(NSString *url)
{
	NSURL *u = [NSURL URLWithString:url];
	NSString *h = [[u host] lowercaseString];

	if (h == nil || !([url hasPrefix:@"http://"] || [url hasPrefix:@"https://"]))
		return nil;
	return [h hasPrefix:@"www."] ? [h substringFromIndex:4] : h;
}

static unsigned hashOf(NSString *s)
{
	const char *c = [s UTF8String];
	unsigned h = 5381;

	while (*c != '\0')
		h = h * 33 + (unsigned char)*c++;
	return h;
}

static NSString *tile(NSString *url, NSString *title, NSString *host)
{
	static const char *palette[] = { "#d94545", "#e68c26", "#339e73", "#3385cc", "#7361cc", "#c74d8c", "#59738c", "#8c804d" };
	NSString *name = [title length] > 0 ? title : host;
	NSString *letter = [[host substringToIndex:1] uppercaseString];

	if ([name length] > 15)
		name = [[name substringToIndex:14] stringByAppendingString:@"..."];
	return [NSString stringWithFormat:@"<a class=\"tile\" href=\"%@\"><b style=\"background:%s\">%@</b><span>%@</span></a>\n",
		html(url), palette[hashOf(host) % 8], html(letter), html(name)];
}

/* up to TILES sites from a store; history keeps the newest entry per host and shows the site's front door */
static NSString *row(NSArray *items, BOOL onePerHost, NSMutableSet *seen)
{
	NSMutableString *out = [NSMutableString string];
	NSUInteger i;
	int n = 0;

	for (i = 0; i < [items count] && n < TILES; i++) {
		NSArray *e = [items objectAtIndex:i];
		NSString *url = [e objectAtIndex:0], *title = [e objectAtIndex:1], *host = hostOf(url);

		if (host == nil || [seen containsObject:host])
			continue;
		if (onePerHost) {
			NSRange r = [url rangeOfString:@"://"];
			NSRange slash = [url rangeOfString:@"/" options:0 range:NSMakeRange(r.location + 3, [url length] - r.location - 3)];
			url = [(slash.location != NSNotFound ? [url substringToIndex:slash.location] : url) stringByAppendingString:@"/"];
			title = host;
			[seen addObject:host];
		}
		[out appendString:tile(url, title, host)];
		n++;
	}
	return out;
}

NSString *FloStartPageURL(void)
{
	NSString *path = startPath();
	NSMutableSet *seen = [NSMutableSet set];
	NSString *marks, *recent;
	NSMutableString *page = [NSMutableString string];
	NSUInteger i;
	NSArray *bm = [[FloStore bookmarks] items];

	[[NSFileManager defaultManager] createDirectoryAtPath:[path stringByDeletingLastPathComponent] withIntermediateDirectories:YES
		attributes:nil error:NULL];
	marks = row(bm, NO, seen);
	for (i = 0; i < [bm count]; i++) {              /* a site shown as a bookmark is not repeated under Recently visited */
		NSString *h = hostOf([[bm objectAtIndex:i] objectAtIndex:0]);
		if (h != nil)
			[seen addObject:h];
	}
	recent = row([[FloStore history] items], YES, seen);

	[page appendString:@"<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>Start Page</title>\n<style>\n"
	  "body { background: #f2f2f2; color: #333; font-family: sans-serif; margin: 0; padding: 36px 0; text-align: center; }\n"
	  "h1 { font-size: 22px; font-weight: normal; color: #555; margin: 0 0 8px 0; }\n"
	  "h2 { font-size: 14px; font-weight: bold; color: #777; margin: 34px 0 12px 0; }\n"
	  ".row { max-width: 1060px; margin: 0 auto; text-align: center; }\n"
	  "a.tile { display: inline-block; width: 112px; margin: 6px 7px; text-decoration: none; color: #333; vertical-align: top; }\n"
	  "a.tile b { display: block; width: 96px; height: 96px; margin: 0 auto; border-radius: 20px; color: #fff; font-size: 48px; "
	  "line-height: 96px; text-align: center; }\n"
	  "a.tile span { display: block; font-size: 12px; margin-top: 6px; height: 16px; overflow: hidden; white-space: nowrap; }\n"
	  ".empty { color: #999; font-size: 13px; margin: 18px 0; }\n"
	  "</style></head><body>\n<h1>Florence</h1>\n<h2>Bookmarks</h2>\n<div class=\"row\">\n"];
	[page appendString:[marks length] > 0 ? marks : @"<p class=\"empty\">Press the star in the toolbar (or Cmd-D) to keep a site here.</p>\n"];
	[page appendString:@"</div>\n<h2>Recently visited</h2>\n<div class=\"row\">\n"];
	[page appendString:[recent length] > 0 ? recent : @"<p class=\"empty\">Sites you visit will appear here.</p>\n"];
	[page appendString:@"</div>\n</body></html>\n"];
	if (![page writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:NULL])
		return nil;
	return [@"file://" stringByAppendingString:path];
}
