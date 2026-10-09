/* Florence: list the X windows (id, mapped or not, geometry, name), two levels deep, to find where an application's window is.   xlistwin
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>

static void list(Display *d, Window w, int depth)
{
	Window root, parent, *kids = NULL;
	unsigned int n = 0, i;

	if (!XQueryTree(d, w, &root, &parent, &kids, &n))
		return;
	for (i = 0; i < n; i++) {
		XWindowAttributes a;
		char *name = NULL;
		if (!XGetWindowAttributes(d, kids[i], &a))
			continue;
		XFetchName(d, kids[i], &name);
		if (a.map_state != IsUnmapped || name != NULL)
			printf("%*s0x%lx %-10s %4dx%-4d at %4d,%-4d %s\n", depth * 2, "", (unsigned long)kids[i],
			       a.map_state == IsViewable ? "viewable" : a.map_state == IsUnviewable ? "unviewable" : "unmapped", a.width, a.height, a.x, a.y, name ? name : "");
		if (name) XFree(name);
		if (depth < 1) list(d, kids[i], depth + 1);
	}
	if (kids) XFree(kids);
}

int main(void)
{
	Display *d = XOpenDisplay(NULL);
	if (!d) { printf("no display\n"); return 1; }
	list(d, DefaultRootWindow(d), 0);
	return 0;
}
