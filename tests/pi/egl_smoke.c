/*
 * Florence: libEGL smoke test, the way WebKit uses EGL: a display, a GL ES 2 context, rendering into a framebuffer object
 * with and without a surface (EGL_KHR_surfaceless_context), a pbuffer, glReadPixels to check the result.
 *   egl_smoke        needs an X server (DISPLAY, XAUTHORITY) because the GLX presenter is an Xlib one.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); } else printf("ok:   "); printf(__VA_ARGS__); printf("\n"); } while (0)

static GLuint compile(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	GLint ok = 0;
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) { char log[512]; glGetShaderInfoLog(s, sizeof log, NULL, log); printf("shader log: %s\n", log); }
	return s;
}

/* draw a green triangle on blue into whatever is bound and check two pixels */
static void draw_and_check(const char *what)
{
	static const GLfloat tri[] = { -1.f, -1.f, 0.f, 1.f,   1.f, -1.f, 0.f, 1.f,   -1.f, 1.f, 0.f, 1.f };
	GLuint prog = glCreateProgram();
	GLint linked = 0;
	unsigned char p[4];

	glAttachShader(prog, compile(GL_VERTEX_SHADER, "attribute vec4 pos; void main() { gl_Position = pos; }"));
	glAttachShader(prog, compile(GL_FRAGMENT_SHADER, "precision mediump float; void main() { gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0); }"));
	glBindAttribLocation(prog, 0, "pos");
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &linked);
	CHECK(linked, "%s: shaders link", what);
	glUseProgram(prog);
	glViewport(0, 0, 64, 64);
	glClearColor(0.f, 0.f, 1.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, tri);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glReadPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
	CHECK(p[0] == 0 && p[1] == 255 && p[2] == 0, "%s: inside the triangle is green (%d,%d,%d)", what, p[0], p[1], p[2]);
	glReadPixels(60, 60, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
	CHECK(p[0] == 0 && p[1] == 0 && p[2] == 255, "%s: outside is the clear colour (%d,%d,%d)", what, p[0], p[1], p[2]);
	CHECK(glGetError() == GL_NO_ERROR, "%s: no GL error", what);
	glDeleteProgram(prog);
}

int main(void)
{
	EGLDisplay dpy;
	EGLint major = 0, minor = 0, n = 0;
	EGLConfig cfg;
	EGLContext ctx;
	EGLSurface pb;
	const EGLint cfg_attr[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
				    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
	const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	const EGLint pb_attr[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
	PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform;
	const char *client_ext = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);

	CHECK(client_ext != NULL && strstr(client_ext, "EGL_EXT_platform_base") != NULL, "client extensions: %s", client_ext ? client_ext : "(none)");
	get_platform = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
	CHECK(get_platform != NULL, "eglGetProcAddress(eglGetPlatformDisplayEXT)");
	dpy = get_platform != NULL ? get_platform(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */, EGL_DEFAULT_DISPLAY, NULL) : eglGetDisplay(EGL_DEFAULT_DISPLAY);
	CHECK(dpy != EGL_NO_DISPLAY, "display");
	CHECK(eglInitialize(dpy, &major, &minor), "eglInitialize: EGL %d.%d (%s)", major, minor, eglQueryString(dpy, EGL_VERSION));
	printf("      vendor %s, extensions: %s\n", eglQueryString(dpy, EGL_VENDOR), eglQueryString(dpy, EGL_EXTENSIONS));
	CHECK(eglBindAPI(EGL_OPENGL_ES_API), "eglBindAPI(OpenGL ES)");
	CHECK(eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n) && n >= 1, "eglChooseConfig: %d matching", n);
	if (n < 1)
		return 1;
	{
		EGLint v = 0, r = 0, g = 0, b = 0, a = 0;
		eglGetConfigAttrib(dpy, cfg, EGL_CONFIG_ID, &v);
		eglGetConfigAttrib(dpy, cfg, EGL_RED_SIZE, &r); eglGetConfigAttrib(dpy, cfg, EGL_GREEN_SIZE, &g);
		eglGetConfigAttrib(dpy, cfg, EGL_BLUE_SIZE, &b); eglGetConfigAttrib(dpy, cfg, EGL_ALPHA_SIZE, &a);
		CHECK(r >= 8 && g >= 8 && b >= 8, "config %d is %d/%d/%d/%d", v, r, g, b, a);
	}
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	CHECK(ctx != EGL_NO_CONTEXT, "eglCreateContext(ES 2): error 0x%x", eglGetError());
	if (ctx == EGL_NO_CONTEXT)
		return 1;

	/* 1. a pbuffer */
	pb = eglCreatePbufferSurface(dpy, cfg, pb_attr);
	CHECK(pb != EGL_NO_SURFACE, "eglCreatePbufferSurface 64x64");
	CHECK(eglMakeCurrent(dpy, pb, pb, ctx), "eglMakeCurrent with the pbuffer");
	CHECK(eglGetCurrentContext() == ctx && eglGetCurrentSurface(EGL_DRAW) == pb && eglGetCurrentDisplay() == dpy, "the current context, surface and display are reported back");
	printf("      GL_RENDERER %s, GL_VERSION %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
	draw_and_check("pbuffer");
	{
		EGLint w = 0, h = 0;
		eglQuerySurface(dpy, pb, EGL_WIDTH, &w);
		eglQuerySurface(dpy, pb, EGL_HEIGHT, &h);
		CHECK(w == 64 && h == 64, "eglQuerySurface: %dx%d", w, h);
	}
	CHECK(eglSwapBuffers(dpy, pb), "eglSwapBuffers on a pbuffer is a no-op that succeeds");

	/* 2. surfaceless: no surface at all, render into a framebuffer object (what WebKit does) */
	CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx), "eglMakeCurrent without a surface (surfaceless)");
	{
		GLuint tex = 0, fbo = 0;
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glGenFramebuffers(1, &fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
		CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "framebuffer object complete");
		draw_and_check("surfaceless fbo");
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDeleteFramebuffers(1, &fbo);
		glDeleteTextures(1, &tex);
	}

	/* 3. shutting down, and the error model */
	CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT), "eglMakeCurrent(none)");
	CHECK(eglDestroySurface(dpy, pb), "eglDestroySurface");
	CHECK(eglDestroyContext(dpy, ctx), "eglDestroyContext");
	CHECK(!eglCreatePbufferSurface(dpy, (EGLConfig)1, pb_attr) && eglGetError() == EGL_BAD_CONFIG, "a bad config is EGL_BAD_CONFIG");
	CHECK(eglTerminate(dpy), "eglTerminate");
	printf("%s (%d failures)\n", fails ? "EGLTEST FAIL" : "EGLTEST PASS", fails);
	return fails != 0;
}
