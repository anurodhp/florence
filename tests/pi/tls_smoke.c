/* SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel) */
/* Does C thread-local storage work on this Darwin? WebKit's WTF/JSC/bmalloc use thread_local everywhere. */
#include <pthread.h>
#include <stdio.h>
static __thread int tl = 7;
static __thread char buf[4096];
static void *worker(void *arg) { tl = 42; buf[100] = 'x'; *(int *)arg = tl + (buf[100] == 'x'); return 0; }
int main(void) {
    int r = 0; pthread_t t; int ok = 1;
    tl = 1; buf[100] = 'm';
    if (pthread_create(&t, 0, worker, &r)) { printf("FAIL pthread_create\n"); return 1; }
    pthread_join(t, 0);
    ok &= (r == 43); ok &= (tl == 1); ok &= (buf[100] == 'm');
    printf("%s tls main=%d worker=%d\n", ok ? "PASS" : "FAIL", tl, r);
    return !ok;
}
