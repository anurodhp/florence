/*
 * Florence: what does a GL frame cost on this GPU stack, and where? Surfaceless GLES 2 context (as WebKit uses it), an RGBA framebuffer
 * the size of the browser's surface, and the pieces of a WebKit frame timed apart: a small draw, glFinish, glReadPixels (RGBA, BGRA, whole
 * and a small rectangle).   gl_readback_bench [width height]   (needs an X server: the EGL layer is a GLX presenter)
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static GLuint compile(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	return s;
}

static void draw_small(void)
{
	static const GLfloat quad[] = { -0.1f, -0.1f, 0.f, 1.f,  0.1f, -0.1f, 0.f, 1.f,  -0.1f, 0.1f, 0.f, 1.f,  0.1f, 0.1f, 0.f, 1.f };
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, quad);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static GLuint prog, tprog, src_tex;

static void draw_full_textured(void)
{
	static const GLfloat quad[] = { -1.f, -1.f, 0.f, 1.f,  1.f, -1.f, 0.f, 1.f,  -1.f, 1.f, 0.f, 1.f,  1.f, 1.f, 0.f, 1.f };
	glUseProgram(tprog);
	glBindTexture(GL_TEXTURE_2D, src_tex);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, quad);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glDisable(GL_BLEND);
	glUseProgram(0);
}

#define N 20
/* modes 6+: 6 three full-screen textured blended quads + glFinish; 7 glTexSubImage2D full RGBA + 100x100 read; 8 same, BGRA_EXT;
 * 9 a 256x256 upload + 100x100 read; 10 three full-screen quads + 100x100 read */
/* mode: 0 draw only, 1 draw+glFinish, 2 draw+read all RGBA, 3 draw+read all BGRA, 4 draw+read 100x100 RGBA, 5 read all RGBA with no draw */
static void run(const char *name, int mode, int w, int h, unsigned char *buf)
{
	double t0, total = 0;
	int i;

	for (i = 0; i < N; i++) {
		t0 = now_ms();
		if (mode == 6 || mode == 10) {
			draw_full_textured(); draw_full_textured(); draw_full_textured();
			glUseProgram(prog);
		} else if (mode == 7) {
			glBindTexture(GL_TEXTURE_2D, src_tex);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
		} else if (mode == 8) {
			glBindTexture(GL_TEXTURE_2D, src_tex);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, 0x80E1, GL_UNSIGNED_BYTE, buf);
		} else if (mode == 9) {
			glBindTexture(GL_TEXTURE_2D, src_tex);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buf);
		} else if (mode != 5)
			draw_small();
		if (mode == 1 || mode == 6)
			glFinish();
		else if (mode >= 7)
			glReadPixels(0, 0, 100, 100, GL_RGBA, GL_UNSIGNED_BYTE, buf);
		else if (mode == 2 || mode == 5)
			glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
		else if (mode == 3)
			glReadPixels(0, 0, w, h, 0x80E1 /* GL_BGRA_EXT */, GL_UNSIGNED_BYTE, buf);
		else if (mode == 4)
			glReadPixels(0, 0, 100, 100, GL_RGBA, GL_UNSIGNED_BYTE, buf);
		total += now_ms() - t0;
	}
	printf("%-34s %7.1f ms per frame\n", name, total / N);
}

int main(int argc, char **argv)
{
	int w = argc > 2 ? atoi(argv[1]) : 960, h = argc > 2 ? atoi(argv[2]) : 602;
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLContext ctx;
	EGLint n = 0;
	const EGLint cfg_attr[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
				    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
	const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
	GLuint tex, fbo;
	unsigned char *buf = malloc((size_t)w * h * 4);

	dpy = get_platform != NULL ? get_platform(0x31DD /* EGL_PLATFORM_SURFACELESS_MESA */, EGL_DEFAULT_DISPLAY, NULL) : eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (!eglInitialize(dpy, NULL, NULL) || !eglBindAPI(EGL_OPENGL_ES_API) || !eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n) || n < 1) {
		printf("EGL setup failed\n");
		return 1;
	}
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		printf("context failed: 0x%x\n", eglGetError());
		return 1;
	}
	printf("GL_RENDERER %s\nGL_VERSION %s\nframebuffer %dx%d\n", glGetString(GL_RENDERER), glGetString(GL_VERSION), w, h);
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	prog = glCreateProgram();
	glAttachShader(prog, compile(GL_VERTEX_SHADER, "attribute vec4 pos; void main() { gl_Position = pos; }"));
	glAttachShader(prog, compile(GL_FRAGMENT_SHADER, "precision mediump float; void main() { gl_FragColor = vec4(0.8, 0.2, 0.2, 1.0); }"));
	glBindAttribLocation(prog, 0, "pos");
	glLinkProgram(prog);
	glUseProgram(prog);
	glViewport(0, 0, w, h);
	glClearColor(1.f, 1.f, 1.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnableVertexAttribArray(0);
	{
		GLuint tp = glCreateProgram();
		glAttachShader(tp, compile(GL_VERTEX_SHADER, "attribute vec4 pos; varying vec2 uv; void main() { gl_Position = pos; uv = pos.xy * 0.5 + 0.5; }"));
		glAttachShader(tp, compile(GL_FRAGMENT_SHADER, "precision mediump float; varying vec2 uv; uniform sampler2D t; void main() { gl_FragColor = texture2D(t, uv) * 0.5; }"));
		glBindAttribLocation(tp, 0, "pos");
		glLinkProgram(tp);
		tprog = tp;
		glGenTextures(1, &src_tex);
		glBindTexture(GL_TEXTURE_2D, src_tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}
	glFinish();
	run("draw small quad (no wait)", 0, w, h, buf);
	run("draw small quad + glFinish", 1, w, h, buf);
	run("draw + glReadPixels all RGBA", 2, w, h, buf);
	run("draw + glReadPixels all BGRA", 3, w, h, buf);
	run("draw + glReadPixels 100x100 RGBA", 4, w, h, buf);
	run("glReadPixels all RGBA, no draw", 5, w, h, buf);
	run("draw + glReadPixels all RGBA (again)", 2, w, h, buf);
	run("3 full-screen blended quads+finish", 6, w, h, buf);
	run("3 full quads + 100x100 read", 10, w, h, buf);
	run("upload 960x602 RGBA + 100x100 read", 7, w, h, buf);
	run("upload 960x602 BGRA + 100x100 read", 8, w, h, buf);
	run("upload 256x256 + 100x100 read", 9, w, h, buf);

	/* the same with a BGRA render target, which is what WebKit's Skia surface is (kBGRA_8888) */
	{
		GLuint tex2, fbo2;
		glGenTextures(1, &tex2);
		glBindTexture(GL_TEXTURE_2D, tex2);
		glGetError();
		glTexImage2D(GL_TEXTURE_2D, 0, 0x80E1 /* GL_BGRA_EXT */, w, h, 0, 0x80E1, GL_UNSIGNED_BYTE, NULL);
		printf("BGRA texture: glTexImage2D error 0x%x\n", glGetError());
		glGenFramebuffers(1, &fbo2);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo2);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex2, 0);
		printf("BGRA framebuffer status 0x%x (complete = 0x%x)\n", glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
			glClear(GL_COLOR_BUFFER_BIT);
			run("BGRA target: draw+finish", 1, w, h, buf);
			run("BGRA target: draw + read 100x100 RGBA", 4, w, h, buf);
			run("BGRA target: draw + read all BGRA", 3, w, h, buf);
			run("BGRA target: draw + read all RGBA", 2, w, h, buf);
		}
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	}

	/* what WebKit's AcceleratedSurface really does: a renderbuffer (GL_RGBA8) colour attachment plus a DEPTH24_STENCIL8 renderbuffer */
	{
		GLuint fbo3, rb_color, rb_ds;
		glGenFramebuffers(1, &fbo3);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo3);
		glGenRenderbuffers(1, &rb_ds);
		glBindRenderbuffer(GL_RENDERBUFFER, rb_ds);
		glRenderbufferStorage(GL_RENDERBUFFER, 0x88F0 /* GL_DEPTH24_STENCIL8_OES */, w, h);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb_ds);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rb_ds);
		glGenRenderbuffers(1, &rb_color);
		glBindRenderbuffer(GL_RENDERBUFFER, rb_color);
		glRenderbufferStorage(GL_RENDERBUFFER, 0x8058 /* GL_RGBA8 */, w, h);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb_color);
		printf("renderbuffer framebuffer status 0x%x, GL error 0x%x\n", glCheckFramebufferStatus(GL_FRAMEBUFFER), glGetError());
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
			glClear(GL_COLOR_BUFFER_BIT);
			run("RENDERBUFFER: draw+finish", 1, w, h, buf);
			run("RENDERBUFFER: draw + read 100x100 RGBA", 4, w, h, buf);
			run("RENDERBUFFER: draw + read all RGBA", 2, w, h, buf);
			run("RENDERBUFFER: draw + read all BGRA", 3, w, h, buf);
		}
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	}
	return 0;
}
