/* Florence: the cairo blit. See FloCairo.h. Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT */
#import "FloCairo.h"
#include <dlfcn.h>
#include <objc/runtime.h>
#include <stdlib.h>

typedef struct { double xx, yx, xy, yy, x0, y0; } flo_matrix;

static struct {
	BOOL tried, ok;
	void *(*image_surface_create_for_data)(const unsigned char *, int format, int w, int h, int stride);
	void (*surface_destroy)(void *);
	void (*save)(void *);
	void (*restore)(void *);
	void (*transform)(void *, const flo_matrix *);
	void (*set_operator)(void *, int);
	void (*set_source_surface)(void *, void *, double, double);
	void *(*get_source)(void *);
	void (*pattern_set_filter)(void *, int);
	void (*rectangle)(void *, double, double, double, double);
	void (*fill)(void *);
} c;

static void *sym(const char *name)
{
	static void *lib;
	void *p = dlsym(RTLD_DEFAULT, name);

	if (p == NULL) {
		if (lib == NULL)
			lib = dlopen("/usr/X11/lib/libcairo.2.dylib", RTLD_LAZY | RTLD_NOLOAD);
		if (lib != NULL)
			p = dlsym(lib, name);
	}
	return p;
}

static BOOL resolve(void)
{
	if (c.tried)
		return c.ok;
	c.tried = YES;
	*(void **)&c.image_surface_create_for_data = sym("cairo_image_surface_create_for_data");
	*(void **)&c.surface_destroy = sym("cairo_surface_destroy");
	*(void **)&c.save = sym("cairo_save");
	*(void **)&c.restore = sym("cairo_restore");
	*(void **)&c.transform = sym("cairo_transform");
	*(void **)&c.set_operator = sym("cairo_set_operator");
	*(void **)&c.set_source_surface = sym("cairo_set_source_surface");
	*(void **)&c.get_source = sym("cairo_get_source");
	*(void **)&c.pattern_set_filter = sym("cairo_pattern_set_filter");
	*(void **)&c.rectangle = sym("cairo_rectangle");
	*(void **)&c.fill = sym("cairo_fill");
	c.ok = c.image_surface_create_for_data && c.surface_destroy && c.save && c.restore && c.transform && c.set_operator &&
	       c.set_source_surface && c.get_source && c.pattern_set_filter && c.rectangle && c.fill;
	return c.ok;
}

/* the instance variable `name` of the object's class or a superclass */
static Ivar ivar(id obj, const char *name) { return obj != nil ? class_getInstanceVariable(object_getClass(obj), name) : NULL; }

BOOL FloCairoDraw(const unsigned char *bgra, int stride, int w, int h, NSRect dirty)
{
	id ctx, gs, ctm;
	Ivar igs, ict, ictm;
	void *ct, *surface;
	NSAffineTransformStruct t;
	flo_matrix m;

	if (!resolve() || bgra == NULL || getenv("FLORENCE_NOCAIRO") != NULL)
		return NO;
	ctx = [NSGraphicsContext currentContext];
	igs = ivar(ctx, "gstate");
	if (igs == NULL || (gs = object_getIvar(ctx, igs)) == nil)
		return NO;
	ict = ivar(gs, "_ct");
	ictm = ivar(gs, "ctm");
	if (ict == NULL || ictm == NULL || (ctm = object_getIvar(gs, ictm)) == nil)
		return NO;
	ct = *(void **)((char *)gs + ivar_getOffset(ict));
	if (ct == NULL)
		return NO;
	t = [ctm transformStruct];
	m.xx = t.m11; m.yx = t.m12; m.xy = t.m21; m.yy = t.m22; m.x0 = t.tX; m.y0 = t.tY;

	surface = c.image_surface_create_for_data(bgra, 1 /* CAIRO_FORMAT_RGB24 */, w, h, stride);
	if (surface == NULL)
		return NO;
	c.save(ct);
	c.set_operator(ct, 1 /* CAIRO_OPERATOR_SOURCE */);
	c.transform(ct, &m);                                    /* the view's transform, as gnustep-back's own drawing applies it */
	c.set_source_surface(ct, surface, 0, 0);
	c.pattern_set_filter(c.get_source(ct), 3 /* CAIRO_FILTER_NEAREST */);
	c.rectangle(ct, dirty.origin.x, dirty.origin.y, dirty.size.width, dirty.size.height);
	c.fill(ct);
	c.restore(ct);
	c.surface_destroy(surface);
	return YES;
}
