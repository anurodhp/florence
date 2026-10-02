/*
 * Florence: NetSurf bitmap table. A NetSurf bitmap is a cairo image surface: the
 * core is told to decode into 32-bit ARGB with premultiplied alpha
 * (BITMAP_LAYOUT_ARGB8888 + pma), which on this little-endian target is exactly
 * cairo's CAIRO_FORMAT_ARGB32, so images are plotted without any conversion.
 * SPDX-License-Identifier: GPL-2.0-only (derived from NetSurf frontends; see LICENSE).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <cairo.h>

#include "utils/errors.h"
#include "netsurf/bitmap.h"
#include "gnustep/gs.h"

struct bitmap {
	cairo_surface_t *surface;
	bool opaque;
};

static void *bitmap_create(int width, int height, enum gui_bitmap_flags flags)
{
	struct bitmap *b = calloc(1, sizeof(*b));
	if (b == NULL)
		return NULL;
	/* cairo zero-fills new image surfaces, so BITMAP_CLEAR is always satisfied */
	b->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
	if (cairo_surface_status(b->surface) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(b->surface);
		free(b);
		return NULL;
	}
	b->opaque = (flags & BITMAP_OPAQUE) != 0;
	return b;
}

static void bitmap_destroy(void *bitmap)
{
	struct bitmap *b = bitmap;
	cairo_surface_destroy(b->surface);
	free(b);
}

static void bitmap_set_opaque(void *bitmap, bool opaque) { ((struct bitmap *)bitmap)->opaque = opaque; }
static bool bitmap_get_opaque(void *bitmap) { return ((struct bitmap *)bitmap)->opaque; }

static unsigned char *bitmap_get_buffer(void *bitmap)
{
	cairo_surface_t *s = ((struct bitmap *)bitmap)->surface;
	cairo_surface_flush(s);
	return cairo_image_surface_get_data(s);
}

static size_t bitmap_get_rowstride(void *bitmap)
{
	return cairo_image_surface_get_stride(((struct bitmap *)bitmap)->surface);
}

static int bitmap_get_width(void *bitmap)
{
	return cairo_image_surface_get_width(((struct bitmap *)bitmap)->surface);
}

static int bitmap_get_height(void *bitmap)
{
	return cairo_image_surface_get_height(((struct bitmap *)bitmap)->surface);
}

static void bitmap_modified(void *bitmap)
{
	cairo_surface_mark_dirty(((struct bitmap *)bitmap)->surface);
}

static nserror bitmap_render(struct bitmap *bitmap, struct hlcache_handle *content)
{
	/* thumbnails of pages: not used by Florence yet */
	return NSERROR_NOT_IMPLEMENTED;
}

static struct gui_bitmap_table bitmap_table = {
	.create = bitmap_create,
	.destroy = bitmap_destroy,
	.set_opaque = bitmap_set_opaque,
	.get_opaque = bitmap_get_opaque,
	.get_buffer = bitmap_get_buffer,
	.get_rowstride = bitmap_get_rowstride,
	.get_width = bitmap_get_width,
	.get_height = bitmap_get_height,
	.modified = bitmap_modified,
	.render = bitmap_render,
};

struct gui_bitmap_table *flo_bitmap_table = &bitmap_table;

void *flo_bitmap_surface(struct bitmap *bm)
{
	return ((struct bitmap *)bm)->surface;
}

void flo_bitmap_init(void)
{
	static const bitmap_fmt_t fmt = { .layout = BITMAP_LAYOUT_ARGB8888, .pma = true };
	bitmap_set_format(&fmt);
}
