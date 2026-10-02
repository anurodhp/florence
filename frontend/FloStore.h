/*
 * Florence: a small persistent list of (address, title): bookmarks and history. One text
 * file per list in ~/.netsurf, a line per entry, "address<TAB>title". Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: GPL-2.0-only
 */
#import <Foundation/Foundation.h>

@interface FloStore : NSObject {
	NSString *path;
	NSMutableArray *items;          /* NSArray *{address, title} */
	NSUInteger cap;
	BOOL newestFirst;
}
+ (FloStore *)bookmarks;                /* ~/.netsurf/Bookmarks, in the order added */
+ (FloStore *)history;                  /* ~/.netsurf/History, newest first, 100 entries */
- (id)initWithFile:(NSString *)name cap:(NSUInteger)cap newestFirst:(BOOL)newestFirst;
- (NSArray *)items;
- (BOOL)contains:(NSString *)address;
- (void)add:(NSString *)address title:(NSString *)title;        /* an existing address is replaced */
- (void)remove:(NSString *)address;
- (void)clear;
@end
