/*
 * Florence: NetSurf plotters on cairo, following the GTK frontend's plotter
 * semantics (frontends/gtk/plotters.c). The cairo_t to draw on is
 * ctx->priv, set up by the page view for each redraw: it already has the
 * dirty rectangle's origin translated to (0,0) and its clip set.
 * SPDX-License-Identifier: GPL-2.0-only (derived from NetSurf frontends; see LICENSE).
 */
#include <math.h>
#include <cairo.h>

#include "utils/errors.h"
#include "netsurf/types.h"
#include "netsurf/plotters.h"
#include "netsurf/plot_style.h"
#include "netsurf/bitmap.h"
#include "gnustep/gs.h"

#define CR(ctx) ((cairo_t *)(ctx)->priv)

static void set_colour(cairo_t *cr, colour c)
{
	cairo_set_source_rgba(cr, (c & 0xff) / 255.0, ((c >> 8) & 0xff) / 255.0, ((c >> 16) & 0xff) / 255.0, 1.0);
}

static void set_stroke(cairo_t *cr, plot_operation_type_t t, plot_style_fixed width)
{
	static const double dot[] = { 1.0, 2.0 }, dash[] = { 8.0, 2.0 };
	switch (t) {
	case PLOT_OP_TYPE_DOT: cairo_set_dash(cr, dot, 2, 0); break;
	case PLOT_OP_TYPE_DASH: cairo_set_dash(cr, dash, 2, 0); break;
	default: cairo_set_dash(cr, NULL, 0, 0); break;
	}
	cairo_set_line_width(cr, width == 0 ? 1.0 : plot_style_fixed_to_double(width));
}

static nserror plot_clip(const struct redraw_context *ctx, const struct rect *clip)
{
	cairo_t *cr = CR(ctx);
	cairo_reset_clip(cr);
	cairo_rectangle(cr, clip->x0, clip->y0, clip->x1 - clip->x0, clip->y1 - clip->y0);
	cairo_clip(cr);
	return NSERROR_OK;
}

static nserror plot_arc(const struct redraw_context *ctx, const plot_style_t *style, int x, int y,
			int radius, int angle1, int angle2)
{
	cairo_t *cr = CR(ctx);
	set_colour(cr, style->fill_colour);
	cairo_set_dash(cr, NULL, 0, 0);
	cairo_set_line_width(cr, 1);
	cairo_arc(cr, x, y, radius, (angle1 + 90) * (M_PI / 180), (angle2 + 90) * (M_PI / 180));
	cairo_stroke(cr);
	return NSERROR_OK;
}

static nserror plot_disc(const struct redraw_context *ctx, const plot_style_t *style, int x, int y, int radius)
{
	cairo_t *cr = CR(ctx);
	if (style->fill_type != PLOT_OP_TYPE_NONE) {
		set_colour(cr, style->fill_colour);
		cairo_new_path(cr);
		cairo_arc(cr, x, y, radius, 0, M_PI * 2);
		cairo_fill(cr);
	}
	if (style->stroke_type != PLOT_OP_TYPE_NONE) {
		set_colour(cr, style->stroke_colour);
		set_stroke(cr, style->stroke_type, style->stroke_width);
		cairo_new_path(cr);
		cairo_arc(cr, x, y, radius, 0, M_PI * 2);
		cairo_stroke(cr);
	}
	return NSERROR_OK;
}

static nserror plot_line(const struct redraw_context *ctx, const plot_style_t *style, const struct rect *line)
{
	cairo_t *cr = CR(ctx);
	set_colour(cr, style->stroke_colour);
	set_stroke(cr, style->stroke_type, style->stroke_width);
	cairo_move_to(cr, (line->x0 == line->x1) ? line->x0 + 0.5 : line->x0,
		      (line->y0 == line->y1) ? line->y0 + 0.5 : line->y0);
	cairo_line_to(cr, (line->x0 == line->x1) ? line->x1 + 0.5 : line->x1,
		      (line->y0 == line->y1) ? line->y1 + 0.5 : line->y1);
	cairo_stroke(cr);
	return NSERROR_OK;
}

static nserror plot_rectangle(const struct redraw_context *ctx, const plot_style_t *style, const struct rect *r)
{
	cairo_t *cr = CR(ctx);
	if (style->fill_type != PLOT_OP_TYPE_NONE) {
		set_colour(cr, style->fill_colour);
		cairo_new_path(cr);
		cairo_rectangle(cr, r->x0, r->y0, r->x1 - r->x0, r->y1 - r->y0);
		cairo_fill(cr);
	}
	if (style->stroke_type != PLOT_OP_TYPE_NONE) {
		set_colour(cr, style->stroke_colour);
		set_stroke(cr, style->stroke_type, style->stroke_width);
		cairo_new_path(cr);
		cairo_rectangle(cr, r->x0 + 0.5, r->y0 + 0.5, r->x1 - r->x0, r->y1 - r->y0);
		cairo_stroke(cr);
	}
	return NSERROR_OK;
}

static nserror plot_polygon(const struct redraw_context *ctx, const plot_style_t *style, const int *p, unsigned int n)
{
	cairo_t *cr = CR(ctx);
	unsigned int i;
	if (n == 0)
		return NSERROR_OK;
	set_colour(cr, style->fill_colour);
	cairo_new_path(cr);
	cairo_move_to(cr, p[0], p[1]);
	for (i = 1; i != n; i++)
		cairo_line_to(cr, p[i * 2], p[i * 2 + 1]);
	cairo_fill(cr);
	return NSERROR_OK;
}

static nserror plot_path(const struct redraw_context *ctx, const plot_style_t *pstyle, const float *p,
			 unsigned int n, const float transform[6])
{
	cairo_t *cr = CR(ctx);
	cairo_matrix_t old, m;
	unsigned int i;

	if (n == 0)
		return NSERROR_OK;
	if (p[0] != PLOTTER_PATH_MOVE)
		return NSERROR_INVALID;

	cairo_get_matrix(cr, &old);
	set_stroke(cr, PLOT_OP_TYPE_SOLID, pstyle->stroke_width);
	m.xx = transform[0]; m.yx = transform[1];
	m.xy = transform[2]; m.yy = transform[3];
	m.x0 = transform[4] + old.x0; m.y0 = transform[5] + old.y0;
	cairo_set_matrix(cr, &m);
	cairo_new_path(cr);
	for (i = 0; i < n;) {
		if (p[i] == PLOTTER_PATH_MOVE) {
			cairo_move_to(cr, p[i + 1], p[i + 2]); i += 3;
		} else if (p[i] == PLOTTER_PATH_CLOSE) {
			cairo_close_path(cr); i++;
		} else if (p[i] == PLOTTER_PATH_LINE) {
			cairo_line_to(cr, p[i + 1], p[i + 2]); i += 3;
		} else if (p[i] == PLOTTER_PATH_BEZIER) {
			cairo_curve_to(cr, p[i + 1], p[i + 2], p[i + 3], p[i + 4], p[i + 5], p[i + 6]); i += 7;
		} else {
			cairo_set_matrix(cr, &old);
			return NSERROR_INVALID;
		}
	}
	cairo_set_matrix(cr, &old);

	if (pstyle->fill_colour != NS_TRANSPARENT) {
		set_colour(cr, pstyle->fill_colour);
		if (pstyle->stroke_colour != NS_TRANSPARENT) {
			cairo_fill_preserve(cr);
			set_colour(cr, pstyle->stroke_colour);
			cairo_stroke(cr);
		} else {
			cairo_fill(cr);
		}
	} else if (pstyle->stroke_colour != NS_TRANSPARENT) {
		set_colour(cr, pstyle->stroke_colour);
		cairo_stroke(cr);
	} else {
		cairo_new_path(cr);
	}
	return NSERROR_OK;
}

static nserror plot_bitmap(const struct redraw_context *ctx, struct bitmap *bitmap, int x, int y,
			   int width, int height, colour bg, bitmap_flags_t flags)
{
	cairo_t *cr = CR(ctx);
	cairo_surface_t *img = flo_bitmap_surface(bitmap);
	bool repeat = (flags & (BITMAPF_REPEAT_X | BITMAPF_REPEAT_Y)) != 0;
	double cx0, cy0, cx1, cy1, sx = 1.0, sy = 1.0;
	int iw, ih;

	if (width <= 0 || height <= 0)
		return NSERROR_OK;
	iw = cairo_image_surface_get_width(img);
	ih = cairo_image_surface_get_height(img);

	/* what to paint: the current clip, limited to the image's own box unless it tiles */
	cairo_save(cr);
	cairo_clip_extents(cr, &cx0, &cy0, &cx1, &cy1);
	if (!(flags & BITMAPF_REPEAT_X)) {
		if (cx0 < x) cx0 = x;
		if (cx1 > x + width) cx1 = x + width;
	}
	if (!(flags & BITMAPF_REPEAT_Y)) {
		if (cy0 < y) cy0 = y;
		if (cy1 > y + height) cy1 = y + height;
	}
	if (cx1 > cx0 && cy1 > cy0) {
		if (iw != width || ih != height) {
			sx = (double)width / iw;
			sy = (double)height / ih;
			cairo_scale(cr, sx, sy);
		}
		cairo_set_source_surface(cr, img, x / sx, y / sy);
		if (repeat)
			cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);
		cairo_pattern_set_filter(cairo_get_source(cr), (sx == 1.0 && sy == 1.0) ? CAIRO_FILTER_NEAREST : CAIRO_FILTER_GOOD);
		cairo_new_path(cr);
		cairo_rectangle(cr, cx0 / sx, cy0 / sy, (cx1 - cx0) / sx, (cy1 - cy0) / sy);
		cairo_fill(cr);
	}
	cairo_restore(cr);
	return NSERROR_OK;
}

static nserror plot_text(const struct redraw_context *ctx, const plot_font_style_t *fstyle, int x, int y,
			 const char *text, size_t length)
{
	cairo_t *cr = CR(ctx);
	cairo_scaled_font_t *sf = flo_scaled_font((const struct plot_font_style *)fstyle);
	cairo_glyph_t *glyphs = NULL;
	cairo_text_cluster_t *clusters = NULL;
	int ng = 0, nc = 0;
	cairo_text_cluster_flags_t fl;

	if (length == 0)
		return NSERROR_OK;
	set_colour(cr, fstyle->foreground);
	cairo_set_scaled_font(cr, sf);
	if (cairo_scaled_font_text_to_glyphs(sf, x, y, text, (int)length, &glyphs, &ng, &clusters, &nc, &fl)
	    == CAIRO_STATUS_SUCCESS && ng > 0)
		cairo_show_glyphs(cr, glyphs, ng);
	cairo_glyph_free(glyphs);
	cairo_text_cluster_free(clusters);
	return NSERROR_OK;
}

static nserror plot_group_start(const struct redraw_context *ctx, const char *name) { return NSERROR_OK; }
static nserror plot_group_end(const struct redraw_context *ctx) { return NSERROR_OK; }
static nserror plot_flush(const struct redraw_context *ctx) { return NSERROR_OK; }

static const struct plotter_table plotters = {
	.clip = plot_clip,
	.arc = plot_arc,
	.disc = plot_disc,
	.line = plot_line,
	.rectangle = plot_rectangle,
	.polygon = plot_polygon,
	.path = plot_path,
	.bitmap = plot_bitmap,
	.text = plot_text,
	.group_start = plot_group_start,
	.group_end = plot_group_end,
	.flush = plot_flush,
	.option_knockout = true,
};

const struct plotter_table *flo_plotters(void)
{
	return &plotters;
}
