/* SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel) */
/* DYLD_INSERT_LIBRARIES shim: logs every __ulock_wait2 / __ulock_wait / __psynch_cvwait call and result to stderr, to see
 * which kernel call libpthread's pthread_cond_timedwait makes on the Pi and what it returns. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdarg.h>
extern int __ulock_wait2(uint32_t op, void *addr, uint64_t value, uint64_t timeout_ns, uint64_t value2);
extern int __ulock_wait(uint32_t op, void *addr, uint64_t value, uint32_t timeout_us);
extern uint32_t __psynch_cvwait(void *cv, uint64_t cvlsgen, uint32_t cvugen, void *mutex, uint64_t mugen, uint32_t flags, int64_t sec, uint32_t nsec);
static void say(const char *s) { write(2, s, strlen(s)); }
static void logf2(const char *fmt, ...) { char b[300]; struct timeval tv; gettimeofday(&tv, 0); int n = snprintf(b, sizeof b, "%ld.%03d p%d ", (long)(tv.tv_sec % 1000), (int)(tv.tv_usec / 1000), (int)getpid()); va_list ap; va_start(ap, fmt); n += vsnprintf(b + n, sizeof b - n, fmt, ap); va_end(ap); say(b); }
static int my_wait2(uint32_t op, void *addr, uint64_t value, uint64_t timeout, uint64_t v2) {
    char b[200]; int r = __ulock_wait2(op, addr, value, timeout, v2); int e = errno;
    snprintf(b, sizeof b, "[ulock_wait2 op=%#x value=%llu timeout_ns=%llu -> %d errno=%d]\n", op, (unsigned long long)value, (unsigned long long)timeout, r, e); say(b); return r; }
static int my_wait(uint32_t op, void *addr, uint64_t value, uint32_t timeout) {
    char b[200]; int r = __ulock_wait(op, addr, value, timeout); int e = errno;
    snprintf(b, sizeof b, "[ulock_wait op=%#x value=%llu timeout_us=%u -> %d errno=%d]\n", op, (unsigned long long)value, timeout, r, e); say(b); return r; }
static uint32_t my_cvwait(void *cv, uint64_t g, uint32_t ug, void *m, uint64_t mg, uint32_t fl, int64_t sec, uint32_t nsec) {
    char b[200]; uint32_t r = __psynch_cvwait(cv, g, ug, m, mg, fl, sec, nsec); int e = errno;
    snprintf(b, sizeof b, "[psynch_cvwait flags=%#x sec=%lld nsec=%u -> %u errno=%d (%#x)]\n", fl, (long long)sec, nsec, r, e, e); say(b); return r; }
static int my_poll(struct pollfd *f, nfds_t n, int t) { int r = poll(f, n, t); if (t != 0) logf2("poll n=%d timeout=%d -> %d\n", (int)n, t, r); return r; }
static ssize_t my_recvmsg(int fd, struct msghdr *m, int fl) { ssize_t r = recvmsg(fd, m, fl); int e = errno; logf2("recvmsg fd=%d -> %ld errno=%d\n", fd, (long)r, e); return r; }
static ssize_t my_sendmsg(int fd, const struct msghdr *m, int fl) { ssize_t r = sendmsg(fd, m, fl); int e = errno; logf2("sendmsg fd=%d len=%ld -> %ld errno=%d\n", fd, (long)(m->msg_iovlen ? m->msg_iov[0].iov_len : 0), (long)r, e); return r; }
static int my_open(const char *p, int fl, ...) { va_list ap; va_start(ap, fl); int mode = va_arg(ap, int); va_end(ap); int r = open(p, fl, mode); if (strncmp(p, "/usr/local/share/florence", 25) == 0 || strncmp(p, "/tmp", 4) == 0) logf2("open %s -> %d\n", p, r); return r; }
typedef struct { const void *replacement; const void *replacee; } interpose_t;
__attribute__((used)) static const interpose_t interposers[] __attribute__((section("__DATA,__interpose"))) = {
    { (const void *)my_wait2, (const void *)__ulock_wait2 },
    { (const void *)my_wait, (const void *)__ulock_wait },
    { (const void *)my_cvwait, (const void *)__psynch_cvwait },
    { (const void *)my_poll, (const void *)poll },
    { (const void *)my_recvmsg, (const void *)recvmsg },
    { (const void *)my_sendmsg, (const void *)sendmsg },
    { (const void *)my_open, (const void *)open },
};
