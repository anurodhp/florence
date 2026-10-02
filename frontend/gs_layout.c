/*
 * Florence: NetSurf layout (text measurement) table, on cairo scaled fonts.
 *
 * Fonts are cairo "toy" faces resolved by fontconfig: the CSS generic family
 * picks the fontconfig alias (sans-serif / serif / monospace), which this
 * port's fontconfig maps to Inter / DejaVu. Sizes: NetSurf passes the size in
 * points as 10-bit fixed point; pixels = pt * 96 / 72 (the same convention the
 * GTK frontend gets from Pango at 96 dpi).
 *
 * Text is shaped once per call with cairo_scaled_font_text_to_glyphs(), whose
 * clusters give the byte offset and x of every character boundary, so width,
 * position and split are all O(length) rather than O(length^2) re-measuring
 * of prefixes.
 * GPL-2.0-only (see gs.h).
 */
#include <stdlib.h>
#include <string.h>
#include <cairo.h>

#include "utils/errors.h"
#include "netsurf/plot_style.h"
#include "netsurf/layout.h"
#include "gnustep/gs.h"

#define FONT_CACHE 48

static struct {
	plot_font_generic_family_t family;
	int bold, slant;
	plot_style_fixed size;
	cairo_scaled_font_t *sf;
} cache[FONT_CACHE];
static int cache_next;
static cairo_t *meas;   /* a 1x1 context to build fonts on */

void *flo_scaled_font(const struct plot_font_style *fsv)
{
	const plot_font_style_t *fs = (const plot_font_style_t *)fsv;
	int bold = fs->weight >= 600;
	int slant = (fs->flags & FONTF_ITALIC) ? 1 : ((fs->flags & FONTF_OBLIQUE) ? 2 : 0);
	int i;

	for (i = 0; i < FONT_CACHE; i++) {
		if (cache[i].sf != NULL && cache[i].family == fs->family && cache[i].bold == bold &&
		    cache[i].slant == slant && cache[i].size == fs->size)
			return cache[i].sf;
	}

	if (meas == NULL) {
		cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
		meas = cairo_create(s);
		cairo_surface_destroy(s);
	}
	const char *name;
	switch (fs->family) {
	case PLOT_FONT_FAMILY_SERIF: name = "serif"; break;
	case PLOT_FONT_FAMILY_MONOSPACE: name = "monospace"; break;
	case PLOT_FONT_FAMILY_CURSIVE: name = "serif"; break;
	default: name = "sans-serif"; break;
	}
	double px = plot_style_fixed_to_double(fs->size) * 96.0 / 72.0;
	if (px < 1.0)
		px = 1.0;
	cairo_save(meas);
	cairo_select_font_face(meas, name,
			       slant == 0 ? CAIRO_FONT_SLANT_NORMAL : (slant == 1 ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_OBLIQUE),
			       bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(meas, px);
	cairo_scaled_font_t *sf = cairo_scaled_font_reference(cairo_get_scaled_font(meas));
	cairo_restore(meas);

	i = cache_next++ % FONT_CACHE;
	if (cache[i].sf != NULL)
		cairo_scaled_font_destroy(cache[i].sf);
	cache[i].family = fs->family;
	cache[i].bold = bold;
	cache[i].slant = slant;
	cache[i].size = fs->size;
	cache[i].sf = sf;
	return sf;
}

struct run {
	cairo_glyph_t *glyphs;
	int nglyphs;
	cairo_text_cluster_t *clusters;
	int nclusters;
	double total;
	/* per cluster: byte offset of its start and x at its start; one extra entry for the end */
	size_t *off;
	double *x;
};

static void run_free(struct run *r)
{
	cairo_glyph_free(r->glyphs);
	cairo_text_cluster_free(r->clusters);
	free(r->off);
	free(r->x);
}

/* utf-8 length to shape: the whole string, or a bounded prefix (on a character
 * boundary) when only the part that can fit in `limit_px` matters. */
static size_t bounded(const char *s, size_t len, int limit_px)
{
	size_t cap;
	if (limit_px < 0)
		return len;
	cap = (size_t)limit_px / 2 + 128;
	if (len <= cap)
		return len;
	while (cap < len && ((unsigned char)s[cap] & 0xC0) == 0x80)
		cap++;
	return cap;
}

static bool shape(const plot_font_style_t *fs, const char *s, size_t len, struct run *r)
{
	cairo_scaled_font_t *sf = flo_scaled_font((const struct plot_font_style *)fs);
	cairo_text_cluster_flags_t flags;
	cairo_status_t st;
	int j, g = 0;
	size_t b = 0;

	memset(r, 0, sizeof(*r));
	if (len == 0)
		return true;
	st = cairo_scaled_font_text_to_glyphs(sf, 0, 0, s, (int)len, &r->glyphs, &r->nglyphs,
					      &r->clusters, &r->nclusters, &flags);
	if (st != CAIRO_STATUS_SUCCESS)
		return false;
	r->off = malloc((r->nclusters + 1) * sizeof(size_t));
	r->x = malloc((r->nclusters + 1) * sizeof(double));
	if (r->off == NULL || r->x == NULL) {
		run_free(r);
		return false;
	}
	if (r->nglyphs > 0) {
		cairo_text_extents_t e;
		cairo_scaled_font_glyph_extents(sf, &r->glyphs[r->nglyphs - 1], 1, &e);
		r->total = r->glyphs[r->nglyphs - 1].x + e.x_advance;
	}
	for (j = 0; j < r->nclusters; j++) {
		r->off[j] = b;
		r->x[j] = (g < r->nglyphs) ? r->glyphs[g].x : r->total;
		b += (size_t)r->clusters[j].num_bytes;
		g += r->clusters[j].num_glyphs;
	}
	r->off[r->nclusters] = b;
	r->x[r->nclusters] = r->total;
	return true;
}

static nserror flo_width(const plot_font_style_t *fs, const char *string, size_t length, int *width)
{
	struct run r;
	if (!shape(fs, string, length, &r)) {
		*width = 0;
		return NSERROR_NOMEM;
	}
	*width = (int)(r.total + 0.5);
	run_free(&r);
	return NSERROR_OK;
}

static nserror flo_position(const plot_font_style_t *fs, const char *string, size_t length, int x,
			    size_t *char_offset, int *actual_x)
{
	struct run r;
	size_t len = bounded(string, length, x);
	int j;

	if (!shape(fs, string, len, &r))
		return NSERROR_NOMEM;
	*char_offset = length;
	*actual_x = (len == length) ? (int)(r.total + 0.5) : x;
	for (j = 0; j < r.nclusters; j++) {
		if (r.x[j + 1] > x) {      /* the cluster containing x */
			*char_offset = r.off[j];
			*actual_x = (int)(r.x[j] + 0.5);
			break;
		}
	}
	run_free(&r);
	return NSERROR_OK;
}

static nserror flo_split(const plot_font_style_t *fs, const char *string, size_t length, int x,
			 size_t *char_offset, int *actual_x)
{
	struct run r;
	size_t len = bounded(string, length, x);
	size_t best = 0, first = 0;
	double best_x = 0, first_x = 0;
	int j;

	if (!shape(fs, string, len, &r))
		return NSERROR_NOMEM;
	if (len == length && r.total <= x) {          /* it all fits */
		*char_offset = length;
		*actual_x = (int)(r.total + 0.5);
		run_free(&r);
		return NSERROR_OK;
	}
	/* break after the last space that fits; if even the first word is too wide,
	 * after its end (Pango's PANGO_WRAP_WORD, as the GTK frontend does) */
	for (j = 0; j < r.nclusters; j++) {
		size_t end = r.off[j + 1];
		if (string[r.off[j]] != ' ')
			continue;
		if (first == 0) {
			first = end;
			first_x = r.x[j + 1];
		}
		if (r.x[j + 1] <= x) {
			best = end;
			best_x = r.x[j + 1];
		}
	}
	if (best != 0) {
		*char_offset = best;
		*actual_x = (int)(best_x + 0.5);
	} else if (first != 0) {
		*char_offset = first;
		*actual_x = (int)(first_x + 0.5);
	} else {
		/* a single word: no break inside it, take it all */
		*char_offset = length;
		if (len == length)
			*actual_x = (int)(r.total + 0.5);
		else
			flo_width(fs, string, length, actual_x);
	}
	run_free(&r);
	return NSERROR_OK;
}

static struct gui_layout_table layout_table = {
	.width = flo_width,
	.position = flo_position,
	.split = flo_split,
};

struct gui_layout_table *flo_layout_table = &layout_table;
