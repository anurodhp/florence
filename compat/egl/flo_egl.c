/*
 * Florence: EGL 1.4 on top of GLX, for WebKit's GL path on a system that has Mesa's client-side GLX (an Xlib presenter, software or
 * VideoCore) but cannot build Mesa's EGL (that wants DRI, DRM and GBM).
 *
 * WPE's web process only ever uses EGL as a way to get a GL ES context and an off-screen target: it renders into framebuffer
 * objects and never presents through EGL. So what is implemented is what that needs, honestly and no more:
 *   - displays: eglGetDisplay / eglGetPlatformDisplay(EXT) for the default, X11 and Mesa-surfaceless platforms; all are one X
 *     connection opened on first eglInitialize (or the Display* the caller passed);
 *   - configs: the GLXFBConfigs of the screen that are RGBA, window or pbuffer capable, with eglChooseConfig's matching and order;
 *   - contexts: OpenGL ES 2 (and 3 when Mesa has it) through GLX_EXT_create_context_es2_profile, or desktop GL;
 *   - surfaces: pbuffers (glXCreatePbuffer) and windows; EGL_KHR_surfaceless_context by making a hidden 1x1 pbuffer current;
 *   - eglGetProcAddress (glXGetProcAddress behind it), eglQueryString, error handling per thread.
 * Not supported, and says so (EGL_BAD_* / EGL_NO_IMAGE): pixmap surfaces, EGLImage (dma-buf import and export), render-to-texture
 * pbuffers, multisample resolve, OpenVG. Fence syncs are glFinish.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPORT __attribute__((visibility("default")))
#define MAX_CONFIGS 64
#define MAGIC_DPY 0x45474c44u
#define MAGIC_CTX 0x45474c43u
#define MAGIC_SRF 0x45474c53u

#ifndef GLX_CONTEXT_MAJOR_VERSION_ARB
#define GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB 0x2092
#define GLX_CONTEXT_FLAGS_ARB 0x2094
#define GLX_CONTEXT_PROFILE_MASK_ARB 0x9126
#endif
#ifndef GLX_CONTEXT_ES2_PROFILE_BIT_EXT
#define GLX_CONTEXT_ES2_PROFILE_BIT_EXT 0x00000004
#endif

struct config {
	GLXFBConfig fbc;
	int id, r, g, b, a, depth, stencil, samples, sample_buffers, visual_id, drawable_type;
};

struct display {
	unsigned magic;
	Display *xdpy;
	int screen, owns_xdpy, initialized;
	struct config configs[MAX_CONFIGS];
	int nconfigs;
	void *(*create_context_attribs)(Display *, GLXFBConfig, GLXContext, Bool, const int *);
	void (*swap_interval)(Display *, GLXDrawable, int);
};

struct context {
	unsigned magic;
	struct display *dpy;
	struct config *config;
	GLXContext glx;
	EGLenum api;
	int version;
	GLXPbuffer dummy;                       /* the 1x1 pbuffer made current for a surfaceless context */
};

struct surface {
	unsigned magic;
	struct display *dpy;
	struct config *config;
	GLXDrawable drawable;
	int is_window, width, height;
	Window window;
};

struct thread {
	EGLint error;
	EGLenum api;
	struct context *ctx;
	struct surface *draw, *read;
	struct display *dpy;
};

static pthread_key_t tkey;
static pthread_once_t tonce = PTHREAD_ONCE_INIT;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct display display_default;

static void tinit(void) { pthread_key_create(&tkey, free); }

static struct thread *thr(void)
{
	struct thread *t;

	pthread_once(&tonce, tinit);
	t = pthread_getspecific(tkey);
	if (t == NULL) {
		t = calloc(1, sizeof *t);
		if (t == NULL)
			return NULL;
		t->error = EGL_SUCCESS;
		t->api = EGL_OPENGL_ES_API;
		pthread_setspecific(tkey, t);
	}
	return t;
}

static EGLBoolean fail(EGLint e)
{
	struct thread *t = thr();
	if (t != NULL)
		t->error = e;
	return EGL_FALSE;
}

static EGLBoolean ok(void)
{
	struct thread *t = thr();
	if (t != NULL)
		t->error = EGL_SUCCESS;
	return EGL_TRUE;
}

static struct display *D(EGLDisplay d)
{
	struct display *p = d;
	return p != NULL && p->magic == MAGIC_DPY ? p : NULL;
}

/* ---- displays ------------------------------------------------------------------------------------------------ */

static EGLDisplay get_display(void *native)
{
	struct display *d = &display_default;

	pthread_mutex_lock(&lock);
	if (d->magic == 0) {
		d->magic = MAGIC_DPY;
		d->xdpy = native != NULL && native != (void *)EGL_DEFAULT_DISPLAY ? native : NULL;     /* a Display* the caller owns */
	}
	pthread_mutex_unlock(&lock);
	return d;
}

EXPORT EGLDisplay eglGetDisplay(EGLNativeDisplayType native) { return get_display((void *)native); }

EXPORT EGLDisplay eglGetPlatformDisplay(EGLenum platform, void *native, const EGLAttrib *attribs)
{
	(void)attribs;
	switch (platform) {
	case EGL_PLATFORM_X11_KHR:
	case 0x31DD:                            /* EGL_PLATFORM_SURFACELESS_MESA */
		return get_display(platform == EGL_PLATFORM_X11_KHR ? native : NULL);
	default:
		fail(EGL_BAD_PARAMETER);
		return EGL_NO_DISPLAY;
	}
}

EXPORT EGLDisplay eglGetPlatformDisplayEXT(EGLenum platform, void *native, const EGLint *attribs)
{
	(void)attribs;
	return eglGetPlatformDisplay(platform, native, NULL);
}

static void load_configs(struct display *d)
{
	int n = 0, i;
	GLXFBConfig *all = glXGetFBConfigs(d->xdpy, d->screen, &n);

	d->nconfigs = 0;
	for (i = 0; all != NULL && i < n && d->nconfigs < MAX_CONFIGS; i++) {
		struct config *c = &d->configs[d->nconfigs];
		int render = 0, drawable = 0, v;

		glXGetFBConfigAttrib(d->xdpy, all[i], GLX_RENDER_TYPE, &render);
		glXGetFBConfigAttrib(d->xdpy, all[i], GLX_DRAWABLE_TYPE, &drawable);
		if (!(render & GLX_RGBA_BIT) || !(drawable & (GLX_WINDOW_BIT | GLX_PBUFFER_BIT)))
			continue;
		memset(c, 0, sizeof *c);
		c->fbc = all[i];
		c->id = d->nconfigs + 1;
		glXGetFBConfigAttrib(d->xdpy, all[i], GLX_RED_SIZE, &c->r);
		glXGetFBConfigAttrib(d->xdpy, all[i], GLX_GREEN_SIZE, &c->g);
		glXGetFBConfigAttrib(d->xdpy, all[i], GLX_BLUE_SIZE, &c->b);
		glXGetFBConfigAttrib(d->xdpy, all[i], GLX_ALPHA_SIZE, &c->a);
		glXGetFBConfigAttrib(d->xdpy, all[i], GLX_DEPTH_SIZE, &c->depth);
		glXGetFBConfigAttrib(d->xdpy, all[i], GLX_STENCIL_SIZE, &c->stencil);
		glXGetFBConfigAttrib(d->xdpy, all[i], GLX_SAMPLES, &c->samples);
		glXGetFBConfigAttrib(d->xdpy, all[i], GLX_SAMPLE_BUFFERS, &c->sample_buffers);
		v = 0;
		glXGetFBConfigAttrib(d->xdpy, all[i], GLX_VISUAL_ID, &v);
		c->visual_id = v;
		c->drawable_type = ((drawable & GLX_WINDOW_BIT) ? EGL_WINDOW_BIT : 0) | ((drawable & GLX_PBUFFER_BIT) ? EGL_PBUFFER_BIT : 0);
		d->nconfigs++;
	}
	if (all != NULL)
		XFree(all);
}

EXPORT EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
	struct display *d = D(dpy);
	int gmaj = 0, gmin = 0;

	if (d == NULL)
		return fail(EGL_BAD_DISPLAY);
	pthread_mutex_lock(&lock);
	if (!d->initialized) {
		if (d->xdpy == NULL) {
			XInitThreads();                 /* the web process makes GL current on its compositor thread */
			d->xdpy = XOpenDisplay(NULL);
			d->owns_xdpy = 1;
		}
		if (d->xdpy == NULL || !glXQueryVersion(d->xdpy, &gmaj, &gmin) || gmaj < 1 || (gmaj == 1 && gmin < 3)) {
			if (d->owns_xdpy && d->xdpy != NULL)
				XCloseDisplay(d->xdpy);
			d->xdpy = NULL;
			pthread_mutex_unlock(&lock);
			return fail(EGL_NOT_INITIALIZED);
		}
		d->screen = DefaultScreen(d->xdpy);
		*(void **)&d->create_context_attribs = (void *)glXGetProcAddress((const GLubyte *)"glXCreateContextAttribsARB");
		*(void **)&d->swap_interval = (void *)glXGetProcAddress((const GLubyte *)"glXSwapIntervalEXT");
		load_configs(d);
		d->initialized = 1;
	}
	pthread_mutex_unlock(&lock);
	if (major != NULL) *major = 1;
	if (minor != NULL) *minor = 4;
	return ok();
}

EXPORT EGLBoolean eglTerminate(EGLDisplay dpy)
{
	struct display *d = D(dpy);

	if (d == NULL)
		return fail(EGL_BAD_DISPLAY);
	/* contexts and surfaces live until destroyed; the connection stays open for them (and for a later eglInitialize) */
	return ok();
}

EXPORT EGLint eglGetError(void)
{
	struct thread *t = thr();
	EGLint e = t != NULL ? t->error : EGL_BAD_ALLOC;

	if (t != NULL)
		t->error = EGL_SUCCESS;
	return e;
}

EXPORT const char *eglQueryString(EGLDisplay dpy, EGLint name)
{
	if (dpy == EGL_NO_DISPLAY) {
		ok();
		if (name == EGL_EXTENSIONS)
			return "EGL_EXT_client_extensions EGL_EXT_platform_base EGL_KHR_platform_x11 EGL_EXT_platform_x11 EGL_MESA_platform_surfaceless";
		fail(EGL_BAD_DISPLAY);
		return NULL;
	}
	if (D(dpy) == NULL || !D(dpy)->initialized) {
		fail(EGL_NOT_INITIALIZED);
		return NULL;
	}
	ok();
	switch (name) {
	case EGL_VENDOR: return "Florence";
	case EGL_VERSION: return "1.4 Florence EGL over GLX";
	case EGL_CLIENT_APIS: return "OpenGL_ES OpenGL";
	case EGL_EXTENSIONS:
		return "EGL_KHR_create_context EGL_KHR_surfaceless_context EGL_KHR_get_all_proc_addresses EGL_KHR_client_get_all_proc_addresses";
	default:
		fail(EGL_BAD_PARAMETER);
		return NULL;
	}
}

/* ---- configs --------------------------------------------------------------------------------------------------- */

EXPORT EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint size, EGLint *num)
{
	struct display *d = D(dpy);
	int i, n;

	if (d == NULL || !d->initialized)
		return fail(d == NULL ? EGL_BAD_DISPLAY : EGL_NOT_INITIALIZED);
	if (num == NULL)
		return fail(EGL_BAD_PARAMETER);
	n = configs == NULL ? d->nconfigs : (size < d->nconfigs ? size : d->nconfigs);
	for (i = 0; configs != NULL && i < n; i++)
		configs[i] = &d->configs[i];
	*num = n;
	return ok();
}

static struct config *C(struct display *d, EGLConfig c)
{
	struct config *p = c;
	return p >= d->configs && p < d->configs + d->nconfigs ? p : NULL;
}

static int config_attrib(struct config *c, EGLint attr, EGLint *value)
{
	switch (attr) {
	case EGL_BUFFER_SIZE: *value = c->r + c->g + c->b + c->a; break;
	case EGL_RED_SIZE: *value = c->r; break;
	case EGL_GREEN_SIZE: *value = c->g; break;
	case EGL_BLUE_SIZE: *value = c->b; break;
	case EGL_ALPHA_SIZE: *value = c->a; break;
	case EGL_DEPTH_SIZE: *value = c->depth; break;
	case EGL_STENCIL_SIZE: *value = c->stencil; break;
	case EGL_SAMPLES: *value = c->samples; break;
	case EGL_SAMPLE_BUFFERS: *value = c->sample_buffers; break;
	case EGL_CONFIG_ID: *value = c->id; break;
	case EGL_SURFACE_TYPE: *value = c->drawable_type; break;
	case EGL_RENDERABLE_TYPE:
	case EGL_CONFORMANT: *value = EGL_OPENGL_ES2_BIT | EGL_OPENGL_BIT; break;
	case EGL_NATIVE_VISUAL_ID: *value = c->visual_id; break;
	case EGL_NATIVE_VISUAL_TYPE: *value = EGL_NONE; break;
	case EGL_NATIVE_RENDERABLE: *value = (c->drawable_type & EGL_WINDOW_BIT) ? EGL_TRUE : EGL_FALSE; break;
	case EGL_MAX_PBUFFER_WIDTH:
	case EGL_MAX_PBUFFER_HEIGHT: *value = 4096; break;
	case EGL_MAX_PBUFFER_PIXELS: *value = 4096 * 4096; break;
	case EGL_MIN_SWAP_INTERVAL: *value = 0; break;
	case EGL_MAX_SWAP_INTERVAL: *value = 1; break;
	case EGL_LEVEL: *value = 0; break;
	case EGL_CONFIG_CAVEAT: *value = EGL_NONE; break;
	case EGL_TRANSPARENT_TYPE: *value = EGL_NONE; break;
	case EGL_TRANSPARENT_RED_VALUE:
	case EGL_TRANSPARENT_GREEN_VALUE:
	case EGL_TRANSPARENT_BLUE_VALUE: *value = 0; break;
	case EGL_BIND_TO_TEXTURE_RGB:
	case EGL_BIND_TO_TEXTURE_RGBA: *value = EGL_FALSE; break;
	case EGL_LUMINANCE_SIZE:
	case EGL_ALPHA_MASK_SIZE: *value = 0; break;
	case EGL_COLOR_BUFFER_TYPE: *value = EGL_RGB_BUFFER; break;
	default: return 0;
	}
	return 1;
}

EXPORT EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value)
{
	struct display *d = D(dpy);
	struct config *c;

	if (d == NULL || !d->initialized)
		return fail(d == NULL ? EGL_BAD_DISPLAY : EGL_NOT_INITIALIZED);
	if ((c = C(d, config)) == NULL)
		return fail(EGL_BAD_CONFIG);
	if (value == NULL || !config_attrib(c, attribute, value))
		return fail(EGL_BAD_ATTRIBUTE);
	return ok();
}

/* EGL 1.4's selection: every given attribute must be met ("at least" for sizes, "contains" for masks, exact otherwise),
 * then sorted by the extra bits a config carries beyond what was asked, then by id. */
static int extras(const struct config *c) { return c->depth + c->stencil + c->samples + c->sample_buffers; }

EXPORT EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attribs, EGLConfig *configs, EGLint size, EGLint *num)
{
	struct display *d = D(dpy);
	struct config *match[MAX_CONFIGS];
	int n = 0, i, j;

	if (d == NULL || !d->initialized)
		return fail(d == NULL ? EGL_BAD_DISPLAY : EGL_NOT_INITIALIZED);
	if (num == NULL)
		return fail(EGL_BAD_PARAMETER);
	for (i = 0; i < d->nconfigs; i++) {
		struct config *c = &d->configs[i];
		const EGLint *a;
		int good = 1;

		for (a = attribs; a != NULL && a[0] != EGL_NONE && good; a += 2) {
			EGLint have;
			EGLint want = a[1];

			if (want == EGL_DONT_CARE)
				continue;
			switch (a[0]) {
			case EGL_RED_SIZE: case EGL_GREEN_SIZE: case EGL_BLUE_SIZE: case EGL_ALPHA_SIZE: case EGL_BUFFER_SIZE:
			case EGL_DEPTH_SIZE: case EGL_STENCIL_SIZE: case EGL_SAMPLES: case EGL_SAMPLE_BUFFERS: case EGL_MAX_PBUFFER_WIDTH:
				config_attrib(c, a[0], &have);
				good = have >= want;
				break;
			case EGL_SURFACE_TYPE:
			case EGL_RENDERABLE_TYPE:
			case EGL_CONFORMANT:
				config_attrib(c, a[0], &have);
				good = (have & want) == want;
				break;
			case EGL_CONFIG_ID:
				good = c->id == want;
				break;
			case EGL_COLOR_BUFFER_TYPE: case EGL_LEVEL: case EGL_TRANSPARENT_TYPE: case EGL_NATIVE_RENDERABLE: case EGL_CONFIG_CAVEAT:
			case EGL_BIND_TO_TEXTURE_RGB: case EGL_BIND_TO_TEXTURE_RGBA:
				if (!config_attrib(c, a[0], &have)) { good = 0; break; }
				good = have == want;
				break;
			case EGL_MATCH_NATIVE_PIXMAP:
				good = 0;
				break;
			default:
				if (config_attrib(c, a[0], &have))
					good = have == want || a[0] == EGL_NATIVE_VISUAL_ID;
				else
					return fail(EGL_BAD_ATTRIBUTE);
			}
		}
		if (good)
			match[n++] = c;
	}
	for (i = 1; i < n; i++)                 /* insertion sort: n is tiny */
		for (j = i; j > 0 && (extras(match[j]) < extras(match[j - 1]) ||
				      (extras(match[j]) == extras(match[j - 1]) && match[j]->id < match[j - 1]->id)); j--) {
			struct config *t = match[j]; match[j] = match[j - 1]; match[j - 1] = t;
		}
	if (configs == NULL) {
		*num = n;
	} else {
		*num = n < size ? n : size;
		for (i = 0; i < *num; i++)
			configs[i] = match[i];
	}
	return ok();
}

/* ---- API binding ---------------------------------------------------------------------------------------------- */

EXPORT EGLBoolean eglBindAPI(EGLenum api)
{
	struct thread *t = thr();

	if (api != EGL_OPENGL_ES_API && api != EGL_OPENGL_API)
		return fail(EGL_BAD_PARAMETER);
	if (t != NULL)
		t->api = api;
	return ok();
}

EXPORT EGLenum eglQueryAPI(void) { struct thread *t = thr(); ok(); return t != NULL ? t->api : EGL_OPENGL_ES_API; }
EXPORT EGLBoolean eglReleaseThread(void) { return ok(); }

/* ---- contexts ------------------------------------------------------------------------------------------------- */

EXPORT EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share, const EGLint *attribs)
{
	struct display *d = D(dpy);
	struct thread *t = thr();
	struct config *c;
	struct context *ctx;
	GLXContext sharedglx = NULL;
	int major = 1, minor = 0, flags = 0;
	const EGLint *a;

	if (d == NULL || !d->initialized) { fail(d == NULL ? EGL_BAD_DISPLAY : EGL_NOT_INITIALIZED); return EGL_NO_CONTEXT; }
	if ((c = C(d, config)) == NULL) { fail(EGL_BAD_CONFIG); return EGL_NO_CONTEXT; }
	if (share != EGL_NO_CONTEXT && ((struct context *)share)->magic == MAGIC_CTX)
		sharedglx = ((struct context *)share)->glx;
	for (a = attribs; a != NULL && a[0] != EGL_NONE; a += 2) {
		switch (a[0]) {
		case EGL_CONTEXT_MAJOR_VERSION_KHR: major = a[1]; break;       /* the same value as EGL_CONTEXT_CLIENT_VERSION */
		case EGL_CONTEXT_MINOR_VERSION_KHR: minor = a[1]; break;
		case EGL_CONTEXT_FLAGS_KHR: flags = a[1]; break;
		case EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR: break;
		default: fail(EGL_BAD_ATTRIBUTE); return EGL_NO_CONTEXT;
		}
	}
	ctx = calloc(1, sizeof *ctx);
	if (ctx == NULL) { fail(EGL_BAD_ALLOC); return EGL_NO_CONTEXT; }
	ctx->magic = MAGIC_CTX;
	ctx->dpy = d;
	ctx->config = c;
	ctx->api = t != NULL ? t->api : EGL_OPENGL_ES_API;
	ctx->version = major;
	if (ctx->api == EGL_OPENGL_ES_API) {
		int try_major[2] = { major < 2 ? 2 : major, 2 }, k;

		if (d->create_context_attribs == NULL) { free(ctx); fail(EGL_BAD_MATCH); return EGL_NO_CONTEXT; }
		for (k = 0; k < 2 && ctx->glx == NULL; k++) {     /* ES 3 first if asked for, then ES 2 */
			int ca[] = { GLX_CONTEXT_MAJOR_VERSION_ARB, try_major[k], GLX_CONTEXT_MINOR_VERSION_ARB, try_major[k] == major ? minor : 0,
				     GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_ES2_PROFILE_BIT_EXT, GLX_CONTEXT_FLAGS_ARB, 0, None };
			(void)flags;
			if (k == 1 && try_major[0] == 2)
				break;
			ctx->glx = d->create_context_attribs(d->xdpy, c->fbc, sharedglx, True, ca);
			ctx->version = try_major[k];
		}
	} else {
		ctx->glx = glXCreateNewContext(d->xdpy, c->fbc, GLX_RGBA_TYPE, sharedglx, True);
	}
	if (ctx->glx == NULL) { free(ctx); fail(EGL_BAD_MATCH); return EGL_NO_CONTEXT; }
	ok();
	return ctx;
}

EXPORT EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext context)
{
	struct display *d = D(dpy);
	struct context *ctx = context;

	if (d == NULL)
		return fail(EGL_BAD_DISPLAY);
	if (ctx == NULL || ctx->magic != MAGIC_CTX)
		return fail(EGL_BAD_CONTEXT);
	if (thr() != NULL && thr()->ctx == ctx)
		eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (ctx->dummy)
		glXDestroyPbuffer(d->xdpy, ctx->dummy);
	glXDestroyContext(d->xdpy, ctx->glx);
	ctx->magic = 0;
	free(ctx);
	return ok();
}

EXPORT EGLBoolean eglQueryContext(EGLDisplay dpy, EGLContext context, EGLint attribute, EGLint *value)
{
	struct context *ctx = context;

	if (D(dpy) == NULL)
		return fail(EGL_BAD_DISPLAY);
	if (ctx == NULL || ctx->magic != MAGIC_CTX || value == NULL)
		return fail(EGL_BAD_CONTEXT);
	switch (attribute) {
	case EGL_CONFIG_ID: *value = ctx->config->id; break;
	case EGL_CONTEXT_CLIENT_TYPE: *value = ctx->api; break;
	case EGL_CONTEXT_CLIENT_VERSION: *value = ctx->version; break;
	case EGL_RENDER_BUFFER: *value = EGL_BACK_BUFFER; break;
	default: return fail(EGL_BAD_ATTRIBUTE);
	}
	return ok();
}

/* ---- surfaces ------------------------------------------------------------------------------------------------- */

EXPORT EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint *attribs)
{
	struct display *d = D(dpy);
	struct config *c;
	struct surface *s;
	int w = 0, h = 0, glxattr[7], n = 0;
	const EGLint *a;

	if (d == NULL || !d->initialized) { fail(d == NULL ? EGL_BAD_DISPLAY : EGL_NOT_INITIALIZED); return EGL_NO_SURFACE; }
	if ((c = C(d, config)) == NULL) { fail(EGL_BAD_CONFIG); return EGL_NO_SURFACE; }
	if (!(c->drawable_type & EGL_PBUFFER_BIT)) { fail(EGL_BAD_MATCH); return EGL_NO_SURFACE; }
	for (a = attribs; a != NULL && a[0] != EGL_NONE; a += 2) {
		switch (a[0]) {
		case EGL_WIDTH: w = a[1]; break;
		case EGL_HEIGHT: h = a[1]; break;
		case EGL_LARGEST_PBUFFER: case EGL_TEXTURE_FORMAT: case EGL_TEXTURE_TARGET: case EGL_MIPMAP_TEXTURE: break;
		default: fail(EGL_BAD_ATTRIBUTE); return EGL_NO_SURFACE;
		}
	}
	if (w <= 0 || h <= 0) { fail(EGL_BAD_ATTRIBUTE); return EGL_NO_SURFACE; }
	glxattr[n++] = GLX_PBUFFER_WIDTH; glxattr[n++] = w;
	glxattr[n++] = GLX_PBUFFER_HEIGHT; glxattr[n++] = h;
	glxattr[n++] = GLX_PRESERVED_CONTENTS; glxattr[n++] = True;
	glxattr[n] = None;
	s = calloc(1, sizeof *s);
	if (s == NULL) { fail(EGL_BAD_ALLOC); return EGL_NO_SURFACE; }
	s->drawable = glXCreatePbuffer(d->xdpy, c->fbc, glxattr);
	if (!s->drawable) { free(s); fail(EGL_BAD_ALLOC); return EGL_NO_SURFACE; }
	s->magic = MAGIC_SRF; s->dpy = d; s->config = c; s->width = w; s->height = h;
	ok();
	return s;
}

EXPORT EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint *attribs)
{
	struct display *d = D(dpy);
	struct config *c;
	struct surface *s;
	Window root; int x, y; unsigned w, h, bw, depth;
	(void)attribs;

	if (d == NULL || !d->initialized) { fail(d == NULL ? EGL_BAD_DISPLAY : EGL_NOT_INITIALIZED); return EGL_NO_SURFACE; }
	if ((c = C(d, config)) == NULL) { fail(EGL_BAD_CONFIG); return EGL_NO_SURFACE; }
	if (!(c->drawable_type & EGL_WINDOW_BIT)) { fail(EGL_BAD_MATCH); return EGL_NO_SURFACE; }
	if (!XGetGeometry(d->xdpy, (Window)win, &root, &x, &y, &w, &h, &bw, &depth)) { fail(EGL_BAD_NATIVE_WINDOW); return EGL_NO_SURFACE; }
	s = calloc(1, sizeof *s);
	if (s == NULL) { fail(EGL_BAD_ALLOC); return EGL_NO_SURFACE; }
	s->drawable = glXCreateWindow(d->xdpy, c->fbc, (Window)win, NULL);
	if (!s->drawable) s->drawable = (GLXDrawable)win;        /* a Window is a GLXDrawable on its own */
	s->magic = MAGIC_SRF; s->dpy = d; s->config = c; s->is_window = 1; s->window = (Window)win; s->width = (int)w; s->height = (int)h;
	ok();
	return s;
}

EXPORT EGLSurface eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config, EGLNativePixmapType pixmap, const EGLint *attribs)
{
	(void)dpy; (void)config; (void)pixmap; (void)attribs;
	fail(EGL_BAD_NATIVE_PIXMAP);
	return EGL_NO_SURFACE;
}

EXPORT EGLSurface eglCreatePlatformWindowSurface(EGLDisplay dpy, EGLConfig config, void *win, const EGLAttrib *attribs)
{
	(void)attribs;
	return eglCreateWindowSurface(dpy, config, win != NULL ? *(EGLNativeWindowType *)win : 0, NULL);
}

EXPORT EGLSurface eglCreatePlatformWindowSurfaceEXT(EGLDisplay dpy, EGLConfig config, void *win, const EGLint *attribs)
{
	(void)attribs;
	return eglCreateWindowSurface(dpy, config, win != NULL ? *(EGLNativeWindowType *)win : 0, NULL);
}

EXPORT EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface)
{
	struct display *d = D(dpy);
	struct surface *s = surface;

	if (d == NULL)
		return fail(EGL_BAD_DISPLAY);
	if (s == NULL || s->magic != MAGIC_SRF)
		return fail(EGL_BAD_SURFACE);
	if (thr() != NULL && (thr()->draw == s || thr()->read == s))
		eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (s->is_window) {
		if (s->drawable != (GLXDrawable)s->window)
			glXDestroyWindow(d->xdpy, s->drawable);
	} else {
		glXDestroyPbuffer(d->xdpy, s->drawable);
	}
	s->magic = 0;
	free(s);
	return ok();
}

EXPORT EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value)
{
	struct surface *s = surface;

	if (D(dpy) == NULL)
		return fail(EGL_BAD_DISPLAY);
	if (s == NULL || s->magic != MAGIC_SRF || value == NULL)
		return fail(EGL_BAD_SURFACE);
	switch (attribute) {
	case EGL_WIDTH:
	case EGL_HEIGHT:
		if (s->is_window) {
			Window root; int x, y; unsigned w, h, bw, depth;
			if (XGetGeometry(s->dpy->xdpy, s->window, &root, &x, &y, &w, &h, &bw, &depth)) { s->width = (int)w; s->height = (int)h; }
		}
		*value = attribute == EGL_WIDTH ? s->width : s->height;
		break;
	case EGL_CONFIG_ID: *value = s->config->id; break;
	case EGL_LARGEST_PBUFFER: *value = EGL_FALSE; break;
	case EGL_RENDER_BUFFER: *value = s->is_window ? EGL_BACK_BUFFER : EGL_BACK_BUFFER; break;
	case EGL_SWAP_BEHAVIOR: *value = EGL_BUFFER_DESTROYED; break;
	case EGL_TEXTURE_FORMAT: *value = EGL_NO_TEXTURE; break;
	case EGL_TEXTURE_TARGET: *value = EGL_NO_TEXTURE; break;
	case EGL_MIPMAP_TEXTURE: *value = EGL_FALSE; break;
	case EGL_PIXEL_ASPECT_RATIO: *value = EGL_DISPLAY_SCALING; break;
	case EGL_HORIZONTAL_RESOLUTION:
	case EGL_VERTICAL_RESOLUTION: *value = EGL_UNKNOWN; break;
	default: return fail(EGL_BAD_ATTRIBUTE);
	}
	return ok();
}

EXPORT EGLBoolean eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value)
{
	(void)dpy; (void)surface; (void)attribute; (void)value;
	return ok();                            /* swap behaviour and multisample resolve are accepted and ignored */
}

EXPORT EGLBoolean eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) { (void)dpy; (void)surface; (void)buffer; return fail(EGL_BAD_MATCH); }
EXPORT EGLBoolean eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) { (void)dpy; (void)surface; (void)buffer; return fail(EGL_BAD_MATCH); }

/* ---- making things current, swapping --------------------------------------------------------------------------- */

EXPORT EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext context)
{
	struct display *d = D(dpy);
	struct thread *t = thr();
	struct context *ctx = context;
	struct surface *sd = draw, *sr = read;
	GLXDrawable gd, gr;

	if (d == NULL || !d->initialized)
		return fail(d == NULL ? EGL_BAD_DISPLAY : EGL_NOT_INITIALIZED);
	if (t == NULL)
		return EGL_FALSE;
	if (context == EGL_NO_CONTEXT) {
		if (draw != EGL_NO_SURFACE || read != EGL_NO_SURFACE)
			return fail(EGL_BAD_MATCH);
		glXMakeContextCurrent(d->xdpy, None, None, NULL);
		t->ctx = NULL; t->draw = t->read = NULL; t->dpy = NULL;
		return ok();
	}
	if (ctx->magic != MAGIC_CTX)
		return fail(EGL_BAD_CONTEXT);
	if ((draw != EGL_NO_SURFACE && sd->magic != MAGIC_SRF) || (read != EGL_NO_SURFACE && sr->magic != MAGIC_SRF))
		return fail(EGL_BAD_SURFACE);
	if (draw == EGL_NO_SURFACE && read == EGL_NO_SURFACE) {
		/* EGL_KHR_surfaceless_context: GLX has no such thing, so a hidden 1x1 pbuffer is the target; rendering goes to FBOs */
		if (!ctx->dummy) {
			int pa[] = { GLX_PBUFFER_WIDTH, 1, GLX_PBUFFER_HEIGHT, 1, None };
			ctx->dummy = glXCreatePbuffer(d->xdpy, ctx->config->fbc, pa);
			if (!ctx->dummy)
				return fail(EGL_BAD_ALLOC);
		}
		gd = gr = ctx->dummy;
	} else if (draw == EGL_NO_SURFACE || read == EGL_NO_SURFACE) {
		return fail(EGL_BAD_MATCH);
	} else {
		gd = sd->drawable; gr = sr->drawable;
	}
	if (!glXMakeContextCurrent(d->xdpy, gd, gr, ctx->glx))
		return fail(EGL_BAD_MATCH);
	t->ctx = ctx; t->draw = sd; t->read = sr; t->dpy = d;
	return ok();
}

EXPORT EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
	struct display *d = D(dpy);
	struct surface *s = surface;

	if (d == NULL)
		return fail(EGL_BAD_DISPLAY);
	if (s == NULL || s->magic != MAGIC_SRF)
		return fail(EGL_BAD_SURFACE);
	if (s->is_window)
		glXSwapBuffers(d->xdpy, s->drawable);
	return ok();                            /* a pbuffer has nothing to swap */
}

EXPORT EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval)
{
	struct display *d = D(dpy);
	struct thread *t = thr();

	if (d == NULL)
		return fail(EGL_BAD_DISPLAY);
	if (t != NULL && t->draw != NULL && t->draw->is_window && d->swap_interval != NULL)
		d->swap_interval(d->xdpy, t->draw->drawable, interval);
	return ok();
}

EXPORT EGLBoolean eglCopyBuffers(EGLDisplay dpy, EGLSurface surface, EGLNativePixmapType target)
{
	(void)dpy; (void)surface; (void)target;
	return fail(EGL_BAD_NATIVE_PIXMAP);
}

EXPORT EGLBoolean eglWaitClient(void) { glXWaitGL(); return ok(); }
EXPORT EGLBoolean eglWaitGL(void) { glXWaitGL(); return ok(); }
EXPORT EGLBoolean eglWaitNative(EGLint engine) { (void)engine; glXWaitX(); return ok(); }

EXPORT EGLContext eglGetCurrentContext(void) { struct thread *t = thr(); return t != NULL && t->ctx != NULL ? (EGLContext)t->ctx : EGL_NO_CONTEXT; }
EXPORT EGLDisplay eglGetCurrentDisplay(void) { struct thread *t = thr(); return t != NULL && t->dpy != NULL ? (EGLDisplay)t->dpy : EGL_NO_DISPLAY; }
EXPORT EGLSurface eglGetCurrentSurface(EGLint which)
{
	struct thread *t = thr();
	struct surface *s = t == NULL ? NULL : which == EGL_DRAW ? t->draw : t->read;
	return s != NULL ? (EGLSurface)s : EGL_NO_SURFACE;
}

/* ---- images and syncs: not supported / glFinish ---------------------------------------------------------------- */

EXPORT EGLImage eglCreateImage(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLAttrib *attribs)
{
	(void)dpy; (void)ctx; (void)target; (void)buffer; (void)attribs;
	fail(EGL_BAD_PARAMETER);
	return EGL_NO_IMAGE;
}
EXPORT EGLImageKHR eglCreateImageKHR(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLint *attribs)
{
	(void)dpy; (void)ctx; (void)target; (void)buffer; (void)attribs;
	fail(EGL_BAD_PARAMETER);
	return EGL_NO_IMAGE_KHR;
}
EXPORT EGLBoolean eglDestroyImage(EGLDisplay dpy, EGLImage image) { (void)dpy; (void)image; return fail(EGL_BAD_PARAMETER); }
EXPORT EGLBoolean eglDestroyImageKHR(EGLDisplay dpy, EGLImageKHR image) { (void)dpy; (void)image; return fail(EGL_BAD_PARAMETER); }

static int fence_dummy;
EXPORT EGLSync eglCreateSync(EGLDisplay dpy, EGLenum type, const EGLAttrib *attribs)
{
	(void)attribs;
	if (D(dpy) == NULL) { fail(EGL_BAD_DISPLAY); return EGL_NO_SYNC; }
	if (type != EGL_SYNC_FENCE) { fail(EGL_BAD_ATTRIBUTE); return EGL_NO_SYNC; }
	glXWaitGL();                            /* the fence is signalled by the time it is returned */
	ok();
	return (EGLSync)&fence_dummy;
}
EXPORT EGLBoolean eglDestroySync(EGLDisplay dpy, EGLSync sync) { (void)dpy; (void)sync; return ok(); }
EXPORT EGLint eglClientWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags, EGLTime timeout) { (void)dpy; (void)sync; (void)flags; (void)timeout; ok(); return EGL_CONDITION_SATISFIED; }
EXPORT EGLBoolean eglGetSyncAttrib(EGLDisplay dpy, EGLSync sync, EGLint attribute, EGLAttrib *value)
{
	(void)dpy; (void)sync;
	if (value == NULL) return fail(EGL_BAD_PARAMETER);
	switch (attribute) {
	case EGL_SYNC_TYPE: *value = EGL_SYNC_FENCE; break;
	case EGL_SYNC_STATUS: *value = EGL_SIGNALED; break;
	case EGL_SYNC_CONDITION: *value = EGL_SYNC_PRIOR_COMMANDS_COMPLETE; break;
	default: return fail(EGL_BAD_ATTRIBUTE);
	}
	return ok();
}

/* ---- proc addresses --------------------------------------------------------------------------------------------- */

static const struct { const char *name; void *fn; } procs[] = {
	{ "eglGetPlatformDisplay", eglGetPlatformDisplay }, { "eglGetPlatformDisplayEXT", eglGetPlatformDisplayEXT },
	{ "eglCreatePlatformWindowSurface", eglCreatePlatformWindowSurface },
	{ "eglCreatePlatformWindowSurfaceEXT", eglCreatePlatformWindowSurfaceEXT },
	{ "eglCreateImage", eglCreateImage }, { "eglCreateImageKHR", eglCreateImageKHR },
	{ "eglDestroyImage", eglDestroyImage }, { "eglDestroyImageKHR", eglDestroyImageKHR },
	{ "eglCreateSync", eglCreateSync }, { "eglDestroySync", eglDestroySync }, { "eglClientWaitSync", eglClientWaitSync },
	{ "eglGetSyncAttrib", eglGetSyncAttrib },
};

EXPORT __eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name)
{
	size_t i;

	if (name == NULL)
		return NULL;
	if (strncmp(name, "egl", 3) == 0) {
		for (i = 0; i < sizeof procs / sizeof procs[0]; i++)
			if (strcmp(procs[i].name, name) == 0)
				return (__eglMustCastToProperFunctionPointerType)procs[i].fn;
		return (__eglMustCastToProperFunctionPointerType)dlsym(RTLD_DEFAULT, name);
	}
	return (__eglMustCastToProperFunctionPointerType)glXGetProcAddress((const GLubyte *)name);
}
