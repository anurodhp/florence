/* Florence: bookmark and history lists. See FloStore.h. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT */
#import "FloStore.h"
#include <stdlib.h>

NSString *FloPrefix(NSString *s, NSUInteger units)
{
	NSUInteger end = 0, n = [s length];

	while (end < n) {
		NSRange r = [s rangeOfComposedCharacterSequenceAtIndex:end];
		if (NSMaxRange(r) > units)
			break;
		end = NSMaxRange(r);
	}
	return [s substringToIndex:end];
}

NSString *FloDropLast(NSString *s)
{
	NSUInteger n = [s length];

	if (n == 0)
		return s;
	return [s substringToIndex:[s rangeOfComposedCharacterSequenceAtIndex:n - 1].location];
}

NSString *FloUTF8Safe(NSString *s)
{
	NSUInteger i, n = [s length];
	NSMutableString *out = nil;

	for (i = 0; i < n; i++) {
		unichar c = [s characterAtIndex:i];
		BOOL pair = c >= 0xd800 && c < 0xdc00 && i + 1 < n &&
			[s characterAtIndex:i + 1] >= 0xdc00 && [s characterAtIndex:i + 1] < 0xe000;

		if (pair) {
			if (out != nil)
				[out appendString:[s substringWithRange:NSMakeRange(i, 2)]];
			i++;
		} else if (c >= 0xd800 && c < 0xe000) {                 /* a lone surrogate */
			if (out == nil)
				out = [NSMutableString stringWithString:[s substringToIndex:i]];
			[out appendString:@"\ufffd"];
		} else if (out != nil) {
			[out appendString:[s substringWithRange:NSMakeRange(i, 1)]];
		}
	}
	return out != nil ? out : s;
}

@implementation FloStore

+ (FloStore *)bookmarks
{
	static FloStore *s;
	if (s == nil)
		s = [[FloStore alloc] initWithFile:@"Bookmarks" cap:500 newestFirst:NO];
	return s;
}

+ (FloStore *)history
{
	static FloStore *s;
	if (s == nil)
		s = [[FloStore alloc] initWithFile:@"History" cap:100 newestFirst:YES];
	return s;
}

- (id)initWithFile:(NSString *)name cap:(NSUInteger)c newestFirst:(BOOL)nf
{
	if ((self = [super init]) == nil)
		return nil;
	/* $HOME, as the engine glue uses for ~/.florence (NSHomeDirectory can differ from it) */
	const char *h = getenv("HOME");
	NSString *home = h != NULL ? [NSString stringWithUTF8String:h] : NSHomeDirectory();
	NSString *dir = [home stringByAppendingPathComponent:@".florence"];
	[[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES
		attributes:nil error:NULL];
	path = [[dir stringByAppendingPathComponent:name] retain];
	cap = c;
	newestFirst = nf;
	items = [[NSMutableArray alloc] init];

	NSString *text = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:NULL];
	NSArray *lines = [text componentsSeparatedByString:@"\n"];
	NSUInteger i;
	for (i = 0; i < [lines count]; i++) {
		NSString *line = [lines objectAtIndex:i];
		NSRange tab = [line rangeOfString:@"\t"];
		NSString *addr = tab.location == NSNotFound ? line : [line substringToIndex:tab.location];
		NSString *title = tab.location == NSNotFound ? @"" : [line substringFromIndex:tab.location + 1];
		if ([addr length] > 0 && [items count] < cap)
			[items addObject:[NSArray arrayWithObjects:addr, title, nil]];
	}
	return self;
}

- (void)dealloc
{
	[path release];
	[items release];
	[super dealloc];
}

- (NSArray *)items { return items; }

- (NSUInteger)indexOf:(NSString *)address
{
	NSUInteger i;
	for (i = 0; i < [items count]; i++)
		if ([[[items objectAtIndex:i] objectAtIndex:0] isEqualToString:address])
			return i;
	return NSNotFound;
}

- (BOOL)contains:(NSString *)address { return [self indexOf:address] != NSNotFound; }

- (void)save
{
	NSMutableString *out = [NSMutableString string];
	NSUInteger i;
	for (i = 0; i < [items count]; i++) {
		NSArray *e = [items objectAtIndex:i];
		[out appendFormat:@"%@\t%@\n", FloUTF8Safe([e objectAtIndex:0]), FloUTF8Safe([e objectAtIndex:1])];
	}
	[out writeToFile:path atomically:YES encoding:NSUTF8StringEncoding error:NULL];
}

- (void)add:(NSString *)address title:(NSString *)title
{
	NSUInteger i;
	if ([address length] == 0)
		return;
	/* a line is the record: no tabs or newlines inside a field */
	address = [[address componentsSeparatedByCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]]
		componentsJoinedByString:@""];
	title = [[[title ? title : @"" componentsSeparatedByCharactersInSet:
		[NSCharacterSet characterSetWithCharactersInString:@"\t\r\n"]] componentsJoinedByString:@" "] copy];
	[title autorelease];
	i = [self indexOf:address];
	if (i != NSNotFound)
		[items removeObjectAtIndex:i];
	if (newestFirst)
		[items insertObject:[NSArray arrayWithObjects:address, title, nil] atIndex:0];
	else
		[items addObject:[NSArray arrayWithObjects:address, title, nil]];
	while ([items count] > cap)
		[items removeObjectAtIndex:newestFirst ? [items count] - 1 : 0];
	[self save];
}

- (void)remove:(NSString *)address
{
	NSUInteger i = [self indexOf:address];
	if (i == NSNotFound)
		return;
	[items removeObjectAtIndex:i];
	[self save];
}

- (void)clear
{
	[items removeAllObjects];
	[self save];
}

@end
