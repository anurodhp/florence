/* SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel) */
/* What does the Pi's __ulock_wait2 / __ulock_wait return when a wait times out? libpthread's ulock condition variable
 * (pthread_cond.c:_pthread_ulock_cond_wait) expects -ETIMEDOUT with ULF_NO_ERRNO. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
extern int __ulock_wait2(uint32_t op, void *addr, uint64_t value, uint64_t timeout_ns, uint64_t value2);
extern int __ulock_wait(uint32_t op, void *addr, uint64_t value, uint32_t timeout_us);
#define UL_COMPARE_AND_WAIT 1
#define ULF_NO_ERRNO 0x01000000
int main(void) {
    static volatile uint32_t word = 0;
    word = 0;   /* the kernel copies the word in with the ulock spinlock held, where a page fault must fail (EFAULT): touch it first */
    (void)word;
    errno = 0; int r = __ulock_wait2(UL_COMPARE_AND_WAIT | ULF_NO_ERRNO, (void *)&word, 0, 20000000ull, 0);
    printf("wait2 NO_ERRNO 20ms: rc=%d errno=%d\n", r, errno);
    errno = 0; r = __ulock_wait2(UL_COMPARE_AND_WAIT, (void *)&word, 0, 20000000ull, 0);
    printf("wait2 (errno mode) 20ms: rc=%d errno=%d\n", r, errno);
    errno = 0; r = __ulock_wait(UL_COMPARE_AND_WAIT | ULF_NO_ERRNO, (void *)&word, 0, 20000);
    printf("wait NO_ERRNO 20ms: rc=%d errno=%d\n", r, errno);
    errno = 0; r = __ulock_wait2(UL_COMPARE_AND_WAIT | ULF_NO_ERRNO, (void *)&word, 1, 20000000ull, 0);
    printf("wait2 value mismatch: rc=%d errno=%d\n", r, errno);
    return 0;
}
