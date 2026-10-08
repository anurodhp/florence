/*
 * Florence: a small persistent list of (address, title): bookmarks and history. One text
 * file per list in ~/.florence, a line per entry, "address<TAB>title". Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#import <Foundation/Foundation.h>

/* Text helpers that never cut a character in two (a surrogate pair, or a letter with its combining marks): cutting
 * one leaves a lone surrogate, which has no UTF-8 form -- [s UTF8String] can be NULL and writing the string to a file fails. */
NSString *FloPrefix(NSString *s, NSUInteger units);     /* at most `units` UTF-16 units, cut on a character boundary */
NSString *FloDropLast(NSString *s);                     /* without its last character */
NSString *FloUTF8Safe(NSString *s);                     /* lone surrogates replaced by U+FFFD */

@interface FloStore : NSObject {
	NSString *path;
	NSMutableArray *items;          /* NSArray *{address, title} */
	NSUInteger cap;
	BOOL newestFirst;
}
+ (FloStore *)bookmarks;                /* ~/.florence/Bookmarks, in the order added */
+ (FloStore *)history;                  /* ~/.florence/History, newest first, 100 entries */
- (id)initWithFile:(NSString *)name cap:(NSUInteger)cap newestFirst:(BOOL)newestFirst;
- (NSArray *)items;
- (BOOL)contains:(NSString *)address;
- (void)add:(NSString *)address title:(NSString *)title;        /* an existing address is replaced */
- (void)remove:(NSString *)address;
- (void)clear;
@end
