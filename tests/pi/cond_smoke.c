/* SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel) */
/* Which timed condition-variable waits work on the Pi's libpthread? GLib (g_cond_wait_until) uses the _relative_np one. */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
int pthread_cond_timedwait_relative_np(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
static int fails;
static void check(const char *name, int rc, int want) { int ok = rc == want; if (!ok) fails++; printf("%s %s: rc=%d (%s)\n", ok ? "PASS" : "FAIL", name, rc, rc == ETIMEDOUT ? "ETIMEDOUT" : rc == EINVAL ? "EINVAL" : rc == 0 ? "ok" : "other"); }
static struct timespec abs_in(long ms) { struct timeval tv; struct timespec t; gettimeofday(&tv, 0); t.tv_sec = tv.tv_sec; t.tv_nsec = tv.tv_usec * 1000 + ms * 1000000; while (t.tv_nsec >= 1000000000) { t.tv_sec++; t.tv_nsec -= 1000000000; } return t; }
static pthread_mutex_t sm = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t sc = PTHREAD_COND_INITIALIZER;
static int flag; static pthread_mutex_t gm = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t gc = PTHREAD_COND_INITIALIZER;
static void *signaler(void *a) { (void)a; usleep(100000); pthread_mutex_lock(&gm); flag = 1; pthread_cond_signal(&gc); pthread_mutex_unlock(&gm); return 0; }
int main(void) {
    struct timespec rel = { 0, 20 * 1000 * 1000 }, ab;
    pthread_mutex_lock(&sm); ab = abs_in(20);
    check("static init: timedwait abs 20ms", pthread_cond_timedwait(&sc, &sm, &ab), ETIMEDOUT);
    check("static init: relative_np 20ms", pthread_cond_timedwait_relative_np(&sc, &sm, &rel), ETIMEDOUT);
    pthread_mutex_unlock(&sm);
    pthread_mutex_t m; pthread_cond_t c; pthread_mutex_init(&m, 0); pthread_cond_init(&c, 0);
    pthread_mutex_lock(&m); ab = abs_in(20);
    check("init(): timedwait abs 20ms", pthread_cond_timedwait(&c, &m, &ab), ETIMEDOUT);
    pthread_mutex_unlock(&m);
    pthread_t t; pthread_create(&t, 0, signaler, 0);
    pthread_mutex_lock(&gm); int rc = 0; ab = abs_in(3000);
    while (!flag && rc == 0) rc = pthread_cond_wait(&gc, &gm);
    check("untimed wait + signal from another thread", rc, 0); pthread_mutex_unlock(&gm); pthread_join(t, 0);
    flag = 0; pthread_create(&t, 0, signaler, 0);
    pthread_mutex_lock(&gm); rc = 0; ab = abs_in(3000);
    while (!flag && rc == 0) rc = pthread_cond_timedwait(&gc, &gm, &ab);
    check("timed wait + signal from another thread", rc, 0); pthread_mutex_unlock(&gm); pthread_join(t, 0);
    printf(fails ? "RESULT FAIL %d\n" : "RESULT OK\n", fails); return fails != 0;
}
