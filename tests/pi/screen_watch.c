/*
 * Florence: watch the screen for flashes. Reads a rectangle of the X root window every <interval ms> for <seconds>, and prints how much of it is
 * black and how much white in each sample; the sample with the most black is written as a PPM. For catching a redraw glitch that is gone before a
 * screenshot can be taken.   screen_watch <seconds> <interval ms> <x> <y> <w> <h> [out.ppm]
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

int main(int argc, char **argv)
{
	double secs = argc > 1 ? atof(argv[1]) : 10, step = argc > 2 ? atof(argv[2]) : 100;
	int x = argc > 3 ? atoi(argv[3]) : 60, y = argc > 4 ? atoi(argv[4]) : 60, w = argc > 5 ? atoi(argv[5]) : 960, h = argc > 6 ? atoi(argv[6]) : 600;
	const char *out = argc > 7 ? argv[7] : "/tmp/watch_worst.ppm";
	Display *d = XOpenDisplay(NULL);
	double t0, worst = -1;
	XImage *keep = NULL;

	if (d == NULL) { printf("no display\n"); return 1; }
	t0 = now_ms();
	while (now_ms() - t0 < secs * 1000) {
		double t = now_ms() - t0;
		XImage *im = XGetImage(d, DefaultRootWindow(d), x, y, w, h, AllPlanes, ZPixmap);
		long black = 0, white = 0, n = (long)w * h;
		int i, j;

		if (im == NULL) continue;
		for (j = 0; j < h; j += 2)
			for (i = 0; i < w; i += 2) {
				unsigned long p = XGetPixel(im, i, j);
				unsigned r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
				if (r < 24 && g < 24 && b < 24) black++;
				else if (r > 235 && g > 235 && b > 235) white++;
			}
		n = (long)((w + 1) / 2) * ((h + 1) / 2);
		printf("t=%6.0f ms  black %5.1f%%  white %5.1f%%\n", t, 100.0 * black / n, 100.0 * white / n);
		fflush(stdout);
		if (100.0 * black / n > worst) { worst = 100.0 * black / n; if (keep) XDestroyImage(keep); keep = im; }
		else XDestroyImage(im);
		{ double wait = step - (now_ms() - t0 - t); if (wait > 0) usleep((useconds_t)(wait * 1000)); }
	}
	if (keep) {
		FILE *f = fopen(out, "w");
		int i, j;
		fprintf(f, "P6\n%d %d\n255\n", w, h);
		for (j = 0; j < h; j++)
			for (i = 0; i < w; i++) {
				unsigned long p = XGetPixel(keep, i, j);
				fputc((p >> 16) & 255, f); fputc((p >> 8) & 255, f); fputc(p & 255, f);
			}
		fclose(f);
		printf("worst sample: %.1f%% black, written to %s\n", worst, out);
	}
	return 0;
}
