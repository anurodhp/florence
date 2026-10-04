/* SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel) */
/* The iokit port's libsystem_pthread returned EINVAL, not ETIMEDOUT, when a pthread_cond_timedwait /
 * pthread_cond_timedwait_relative_np actually timed out (a wait that is signalled in time, and untimed waits, were fine:
 * tests/pi/cond_smoke.c). GLib (g_cond_wait_until -> g_thread_abort "Invalid argument") and everything built on timed
 * waits died on it. ROOT CAUSE (found with tests/pi/interpose_sync.c and errno_slot.c): the port's cerror_stub.c gave every
 * thread one global errno while libpthread reads errno through its per-thread TSD slot 1 (third_party/libpthread/src/
 * pthread_cond.c:_pthread_psynch_cond_wait, `switch (err & 0xff)`), so it saw a stale cell. FIXED in the iokit repo
 * (cerror_stub.c: __error() returns the TSD slot, PR on anurodhp/iokit); this wrapper stays only until the Pi image has the
 * rebuilt libsystem_kernel.dylib, then delete this file. It is harmless with the fix (the real call then returns ETIMEDOUT).
 * Workaround: call the real function and, when it says EINVAL after the deadline has passed, say ETIMEDOUT (the mutex
 * is re-locked on that path in the real code). Link order puts libflocompat before libsystem_pthread, so every image of
 * this tree binds to these. Debt: fix libpthread/the kernel error plumbing in the iokit port and delete this file. */
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>

typedef int (*abs_fn)(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
static abs_fn real_abs, real_rel;

static void *real(const char *name) {
    void *h = dlopen("/usr/lib/system/libsystem_pthread.dylib", RTLD_NOW | RTLD_NOLOAD);
    if (!h) h = dlopen("/usr/lib/system/libsystem_pthread.dylib", RTLD_NOW);
    return h ? dlsym(h, name) : 0;
}
static void now_real(struct timespec *t) { struct timeval tv; gettimeofday(&tv, 0); t->tv_sec = tv.tv_sec; t->tv_nsec = tv.tv_usec * 1000; }
static int not_before(const struct timespec *a, const struct timespec *b) { return a->tv_sec > b->tv_sec || (a->tv_sec == b->tv_sec && a->tv_nsec >= b->tv_nsec); }

int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *abstime) {
    if (!real_abs) real_abs = (abs_fn)real("pthread_cond_timedwait");
    if (!real_abs) return EINVAL;
    int rc = real_abs(c, m, abstime);
    if (rc == EINVAL && abstime) { struct timespec n; now_real(&n); if (not_before(&n, abstime)) return ETIMEDOUT; }
    return rc;
}
int pthread_cond_timedwait_relative_np(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *rel) {
    if (!real_rel) real_rel = (abs_fn)real("pthread_cond_timedwait_relative_np");
    if (!real_rel) return EINVAL;
    struct timespec start, deadline; now_real(&start);
    deadline.tv_sec = start.tv_sec + (rel ? rel->tv_sec : 0); deadline.tv_nsec = start.tv_nsec + (rel ? rel->tv_nsec : 0);
    if (deadline.tv_nsec >= 1000000000) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000; }
    int rc = real_rel(c, m, rel);
    if (rc == EINVAL && rel) { struct timespec n; now_real(&n); if (not_before(&n, &deadline)) return ETIMEDOUT; }
    return rc;
}
