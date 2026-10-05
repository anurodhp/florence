/*
 * Florence: the start page. A new tab or window without a homepage opens a local page: a row of tiles for the bookmarks and,
 * below it, a row of recently visited sites, like Safari's. It is plain HTML and CSS written from the two store files, so it
 * needs no engine support and no scripts. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#import <Foundation/Foundation.h>

NSString *FloStartPageURL(void);                /* writes ~/.florence/start/index.html; its file: URL */
BOOL FloIsStartPageURL(NSString *url);
