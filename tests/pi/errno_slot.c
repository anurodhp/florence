/* SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel) */
/* libpthread reads errno through thread-specific-data slot 1 (an int* the C library installs); the C library's errno is
 * *__error(). On Darwin they are the same int. Are they here, on the main thread and on a new thread? */
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
static int *slot1(void) { uintptr_t tsd; __asm__("mrs %0, TPIDRRO_EL0" : "=r"(tsd)); tsd &= ~(uintptr_t)7; return (int *)((uintptr_t *)tsd)[1]; }
static int fails;
static void check(const char *who) {
    errno = 4242; int *s = slot1(); int via_slot = s ? *s : -1;
    int ok = (s == &errno) && via_slot == 4242; if (!ok) fails++;
    printf("%s %s: &errno=%p slot1=%p *slot1=%d (errno set to 4242)\n", ok ? "PASS" : "FAIL", who, (void *)&errno, (void *)s, via_slot);
}
static void *worker(void *a) { (void)a; check("secondary thread"); return 0; }
int main(void) { check("main thread"); pthread_t t; pthread_create(&t, 0, worker, 0); pthread_join(t, 0); printf(fails ? "RESULT FAIL\n" : "RESULT OK\n"); return fails != 0; }
