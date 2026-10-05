/*
 * Florence: diagnostics for the one failure that has no message of its own. An X connection error ("fatal IO error 22" or 60)
 * ends the process inside Xlib without a hint of who was talking to the server; Xlib lets us look first. Plain C, no WebKit.
 * Copyright (c) 2026 Anurodh Pokharel. SPDX-License-Identifier: MIT
 */
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "flo.h"

/* An X connection error ("fatal IO error 22") normally ends the process inside Xlib with no hint of
 * who was talking to the server. Xlib lets us look first: say which thread and errno, and walk the
 * frames (frame pointers; same address notes as the crash report above). XSetIOErrorHandler is found
 * at run time because libX11 belongs to the gnustep-back bundle, not to this executable. */
static int (*x_connection_number)(void *);

/* "name+0x1c in /path/lib" for a code address, through dladdr() found at run time (no link dependency) */
static void describe_addr(int n, uintptr_t pc)
{
	typedef int (*dladdr_fn)(const void *, Dl_info *);
	static dladdr_fn da;
	static bool looked;
	Dl_info di;

	if (!looked) {
		looked = true;
		da = (dladdr_fn)dlsym(RTLD_DEFAULT, "dladdr");
	}
	if (da != NULL && da((const void *)pc, &di) != 0 && di.dli_fname != NULL)
		fprintf(stderr, "florence:   #%d %p  %s+%#lx  (%s)\n", n, (void *)pc,
			di.dli_sname != NULL ? di.dli_sname : "?",
			di.dli_saddr != NULL ? (unsigned long)(pc - (uintptr_t)di.dli_saddr) : 0UL, di.dli_fname);
	else
		fprintf(stderr, "florence:   #%d %p\n", n, (void *)pc);
}

/* what an open descriptor is, for the X-connection post-mortem */
static void describe_fd(const char *what, int fd)
{
	struct stat st;
	int type = -1, listening = 0, err = 0;
	socklen_t l;

	if (fstat(fd, &st) != 0) {
		fprintf(stderr, "florence: %s fd %d: not open (%s)\n", what, fd, strerror(errno));
		return;
	}
	if (S_ISSOCK(st.st_mode)) {
		l = sizeof(type);
		getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &l);
		l = sizeof(listening);
		getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &listening, &l);
		l = sizeof(err);
		getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &l);
		fprintf(stderr, "florence: %s fd %d: socket type %d%s, pending error %d\n", what, fd, type,
			listening ? ", LISTENING" : "", err);
	} else {
		fprintf(stderr, "florence: %s fd %d: not a socket (mode %o)\n", what, fd, (unsigned)st.st_mode);
	}
}

static int on_xio_error(void *display)
{
	int e = errno;
	uintptr_t *f = (uintptr_t *)__builtin_frame_address(0);
	int i, fd = -1, socks = 0, listen_socks = 0, open_fds = 0;
	struct stat st;

	fprintf(stderr, "florence: X IO error: errno %d (%s), %s thread\n", e, strerror(e),
		pthread_main_np() ? "main" : "a secondary");
	if (x_connection_number != NULL && display != NULL) {
		fd = x_connection_number(display);
		describe_fd("X connection", fd);
	}
	for (i = 0; i < 1024; i++) {
		if (fstat(i, &st) != 0)
			continue;
		open_fds++;
		if (S_ISSOCK(st.st_mode)) {
			int l2 = 0;
			socklen_t l = sizeof(l2);

			socks++;
			if (getsockopt(i, SOL_SOCKET, SO_ACCEPTCONN, &l2, &l) == 0 && l2)
				listen_socks++;
		}
	}
	fprintf(stderr, "florence: %d descriptors open, %d sockets, %d listening\n", open_fds, socks, listen_socks);
	fprintf(stderr, "florence: flo_install_xio_handler is at %p (slide = this - nm address)\n", (void *)flo_install_xio_handler);
	for (i = 0; i < 40 && f != NULL && ((uintptr_t)f & 7) == 0 && (uintptr_t)f > 0x10000; i++) {
		describe_addr(i, f[1]);
		if ((uintptr_t *)f[0] <= f)
			break;
		f = (uintptr_t *)f[0];
	}
	fflush(stderr);
	_exit(1);
}

void flo_install_xio_handler(void)
{
	typedef void *(*set_fn)(int (*)(void *));
	static const char *const libs[] = { "/usr/X11/lib/libX11.6.dylib", "/usr/lib/libX11.6.dylib", NULL };
	set_fn set = (set_fn)dlsym(RTLD_DEFAULT, "XSetIOErrorHandler");
	int i;

	for (i = 0; set == NULL && libs[i] != NULL; i++) {
		void *h = dlopen(libs[i], RTLD_LAZY | RTLD_NOLOAD);

		if (h != NULL)
			set = (set_fn)dlsym(h, "XSetIOErrorHandler");
	}
	if (set != NULL)
		set(on_xio_error);
	x_connection_number = (int (*)(void *))dlsym(RTLD_DEFAULT, "XConnectionNumber");
	for (i = 0; x_connection_number == NULL && libs[i] != NULL; i++) {
		void *h = dlopen(libs[i], RTLD_LAZY | RTLD_NOLOAD);

		if (h != NULL)
			x_connection_number = (int (*)(void *))dlsym(h, "XConnectionNumber");
	}
}

/* ---- crash report ---------------------------------------------------------------------------------
 * There is no debugger on the target. On a fatal signal print the fault address, pc, lr and a frame-pointer walk with
 * symbols (dladdr), and the run-time address of flo_install_xio_handler: subtract the address `nm` shows for it in the
 * executable to get the load slide, then look the executable's addresses up on the build host. */
static void on_crash(int sig, siginfo_t *si, void *ctx)
{
	fprintf(stderr, "florence: CRASH signal %d, fault address %p, %s thread\n", sig, si != NULL ? si->si_addr : NULL,
		pthread_main_np() ? "main" : "a secondary");
	fprintf(stderr, "florence: flo_install_xio_handler is at %p (slide = this - nm address)\n", (void *)flo_install_xio_handler);
#if defined(__APPLE__) && defined(__arm64__)
	{
		ucontext_t *uc = ctx;
		uintptr_t pc = (uintptr_t)__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
		uintptr_t lr = (uintptr_t)__darwin_arm_thread_state64_get_lr(uc->uc_mcontext->__ss);
		uintptr_t *f = (uintptr_t *)__darwin_arm_thread_state64_get_fp(uc->uc_mcontext->__ss);
		int i;

		describe_addr(0, pc);
		describe_addr(1, lr);
		for (i = 2; i < 40 && f != NULL && ((uintptr_t)f & 7) == 0 && (uintptr_t)f > 0x10000; i++) {
			describe_addr(i, f[1]);
			if ((uintptr_t *)f[0] <= f)
				break;
			f = (uintptr_t *)f[0];
		}
	}
#else
	(void)ctx;
#endif
	fflush(stderr);
	signal(sig, SIG_DFL);    /* returning re-runs the faulting instruction: the usual death */
}

void flo_install_crash_report(void)
{
	static const int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
	struct sigaction sa;
	size_t i;

	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = on_crash;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	for (i = 0; i < sizeof sigs / sizeof sigs[0]; i++)
		sigaction(sigs[i], &sa, NULL);
}
