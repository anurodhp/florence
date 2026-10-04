/* SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel) */
/* Does the EINVAL on a timed-out pthread_cond_timedwait depend on the mutex policy (ulock vs psynch cond path)? */
#include <errno.h>
#include <pthread.h>
#include <pthread/pthread_spis.h>
#include <stdio.h>
#include <sys/time.h>
static struct timespec abs_in(long ms) { struct timeval tv; struct timespec t; gettimeofday(&tv, 0); t.tv_sec = tv.tv_sec; t.tv_nsec = tv.tv_usec * 1000 + ms * 1000000; while (t.tv_nsec >= 1000000000) { t.tv_sec++; t.tv_nsec -= 1000000000; } return t; }
static void run(const char *name, int policy) {
    pthread_mutexattr_t a; pthread_mutex_t m; pthread_cond_t c; pthread_mutexattr_init(&a);
    if (policy >= 0) pthread_mutexattr_setpolicy_np(&a, policy);
    pthread_mutex_init(&m, &a); pthread_cond_init(&c, 0);
    pthread_mutex_lock(&m); struct timespec ab = abs_in(20);
    int rc = pthread_cond_timedwait(&c, &m, &ab);
    printf("%s: rc=%d (%s)\n", name, rc, rc == ETIMEDOUT ? "ETIMEDOUT" : rc == EINVAL ? "EINVAL" : "other");
    pthread_mutex_unlock(&m);
}
int main(void) {
    run("default policy", -1);
    run("FAIRSHARE", _PTHREAD_MUTEX_POLICY_FAIRSHARE);
    run("FIRSTFIT", _PTHREAD_MUTEX_POLICY_FIRSTFIT);
    return 0;
}
