/* SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel) */
/* POSIX unnamed semaphores (sem_init/sem_destroy/sem_post/sem_wait). Darwin does not implement them at all (only named
 * ones, sem_open), and its sem_t is a 4-byte int, too small to hold a mutex and condition variable; Skia's Unix
 * SkSemaphore (which WebKit's Skia selects, SK_BUILD_FOR_UNIX) embeds a sem_t and calls these. The int stores an index
 * into a table of heap-allocated semaphores. Debt: real Darwin code uses dispatch semaphores instead (SkSemaphore_mac). */
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

typedef int flo_sem_t;
struct flo_sem { pthread_mutex_t m; pthread_cond_t c; unsigned value; };
static pthread_mutex_t table_lock = PTHREAD_MUTEX_INITIALIZER;
static struct flo_sem **table; static int table_size;

static struct flo_sem *lookup(flo_sem_t *s) {
    pthread_mutex_lock(&table_lock);
    int id = *s;
    struct flo_sem *r = (id > 0 && id <= table_size) ? table[id - 1] : 0;
    pthread_mutex_unlock(&table_lock);
    return r;
}
int sem_init(flo_sem_t *s, int pshared, unsigned value) {
    (void)pshared;
    struct flo_sem *x = malloc(sizeof *x);
    if (!x) { errno = ENOSPC; return -1; }
    pthread_mutex_init(&x->m, 0); pthread_cond_init(&x->c, 0); x->value = value;
    pthread_mutex_lock(&table_lock);
    int id = 0;
    for (int i = 0; i < table_size; i++) if (!table[i]) { id = i + 1; break; }
    if (!id) {
        struct flo_sem **n = realloc(table, (size_t)(table_size ? table_size * 2 : 64) * sizeof *n);
        if (!n) { pthread_mutex_unlock(&table_lock); free(x); errno = ENOSPC; return -1; }
        int old = table_size, ns = table_size ? table_size * 2 : 64;
        for (int i = old; i < ns; i++) n[i] = 0;
        table = n; table_size = ns; id = old + 1;
    }
    table[id - 1] = x; *s = id;
    pthread_mutex_unlock(&table_lock);
    return 0;
}
int sem_destroy(flo_sem_t *s) {
    pthread_mutex_lock(&table_lock);
    int id = *s; struct flo_sem *x = (id > 0 && id <= table_size) ? table[id - 1] : 0;
    if (x) table[id - 1] = 0;
    pthread_mutex_unlock(&table_lock);
    if (!x) { errno = EINVAL; return -1; }
    pthread_cond_destroy(&x->c); pthread_mutex_destroy(&x->m); free(x);
    return 0;
}
int sem_post(flo_sem_t *s) {
    struct flo_sem *x = lookup(s);
    if (!x) { errno = EINVAL; return -1; }
    pthread_mutex_lock(&x->m); x->value++; pthread_cond_signal(&x->c); pthread_mutex_unlock(&x->m);
    return 0;
}
int sem_wait(flo_sem_t *s) {
    struct flo_sem *x = lookup(s);
    if (!x) { errno = EINVAL; return -1; }
    pthread_mutex_lock(&x->m);
    while (x->value == 0) pthread_cond_wait(&x->c, &x->m);
    x->value--;
    pthread_mutex_unlock(&x->m);
    return 0;
}
int sem_trywait(flo_sem_t *s) {
    struct flo_sem *x = lookup(s);
    if (!x) { errno = EINVAL; return -1; }
    int ok = 0;
    pthread_mutex_lock(&x->m); if (x->value) { x->value--; ok = 1; } pthread_mutex_unlock(&x->m);
    if (!ok) { errno = EAGAIN; return -1; }
    return 0;
}
