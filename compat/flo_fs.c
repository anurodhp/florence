/* SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel) */
/* Filesystem calls the iokit port's libSystem does not export but its kernel implements: needed by libc++'s
 * std::filesystem (WebKit's WTF::FileSystem uses it) and by anything using the *at family. Each stub is the raw BSD
 * syscall: number in x16, svc #0x80, carry set = failure with errno in x0. Numbers are third_party/xnu-7195/
 * bsd/kern/syscalls.master (line in the comment). Debt: these belong in libsystem_kernel; delete them as they land. */
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

__asm__(".text\n.align 2\n"
        ".globl _flo_sys_error\n_flo_sys_error:\n"          /* x0 = errno from the kernel: set it, return -1 */
        "    stp x29, x30, [sp, #-32]!\n    mov x29, sp\n    str x0, [sp, #16]\n"
        "    bl ___error\n    ldr x1, [sp, #16]\n    str w1, [x0]\n    mov x0, #-1\n    ldp x29, x30, [sp], #32\n    ret\n");
#define FLO_SYSCALL(name, num) \
    __asm__(".text\n.align 2\n.globl _" #name "\n_" #name ":\n    mov x16, #" #num "\n    svc #0x80\n    b.cc 1f\n    b _flo_sys_error\n1:  ret\n");

FLO_SYSCALL(symlink, 57)        /* :110  */
FLO_SYSCALL(futimes, 139)       /* futimes(int fd, const struct timeval[2]) */
FLO_SYSCALL(truncate, 200)      /* :291  */
FLO_SYSCALL(faccessat, 466)     /* :739  */
FLO_SYSCALL(fchmodat, 467)      /* :740  */
FLO_SYSCALL(fchownat, 468)      /* :741  */
FLO_SYSCALL(linkat, 471)        /* :744  */
FLO_SYSCALL(unlinkat, 472)      /* :745  */
FLO_SYSCALL(readlinkat, 473)    /* :746  */
FLO_SYSCALL(symlinkat, 474)     /* :747  */

/* utimensat: xnu-7195 has no such syscall (Apple's Libc builds it on setattrlistat). Over utimes() for the AT_FDCWD /
 * absolute-path case, microsecond resolution; a directory fd with a relative path is ENOSYS. UTIME_NOW/UTIME_OMIT
 * (the two special tv_nsec values) are handled as in POSIX. */
#define FLO_UTIME_NOW  (-1)
#define FLO_UTIME_OMIT (-2)
int utimes(const char *, const struct timeval *);
int utimensat(int fd, const char *path, const struct timespec ts[2], int flag) {
    (void)flag;
    if (fd != -2 /* AT_FDCWD on Darwin */ && path[0] != '/') { errno = ENOSYS; return -1; }
    struct timeval tv[2];
    struct stat st;
    if (!ts) { return utimes(path, 0); }
    for (int i = 0; i < 2; i++) {
        if (ts[i].tv_nsec == FLO_UTIME_NOW) { gettimeofday(&tv[i], 0); continue; }
        if (ts[i].tv_nsec == FLO_UTIME_OMIT) {
            if (stat(path, &st) != 0) return -1;
            tv[i].tv_sec = i == 0 ? st.st_atimespec.tv_sec : st.st_mtimespec.tv_sec;
            tv[i].tv_usec = (i == 0 ? st.st_atimespec.tv_nsec : st.st_mtimespec.tv_nsec) / 1000;
            continue;
        }
        tv[i].tv_sec = ts[i].tv_sec; tv[i].tv_usec = ts[i].tv_nsec / 1000;
    }
    return utimes(path, tv);
}

/* realpath(3): canonical absolute path, symlinks resolved, every component must exist. Apple's Libc one needs libc
 * internals (namespace.h, getattrlist); this is the classic algorithm. Exported under both names the SDK headers use. */
static char *flo_rp(const char *path, char *resolved) {
    char *out = resolved ? resolved : malloc(PATH_MAX);
    char left[PATH_MAX], next[PATH_MAX], link[PATH_MAX];
    int symlinks = 0;
    size_t llen, rlen;
    if (!out) return 0;
    if (!path || !*path) { errno = path ? ENOENT : EINVAL; goto fail; }
    if (strlen(path) >= PATH_MAX) { errno = ENAMETOOLONG; goto fail; }
    if (path[0] == '/') { out[0] = '/'; out[1] = 0; rlen = 1; strcpy(left, path + 1); }
    else { if (!getcwd(out, PATH_MAX)) goto fail; rlen = strlen(out); strcpy(left, path); }
    llen = strlen(left);
    while (llen > 0) {
        char *p = strchr(left, '/');
        size_t n = p ? (size_t)(p - left) : llen;
        if (n >= PATH_MAX) { errno = ENAMETOOLONG; goto fail; }
        memcpy(next, left, n); next[n] = 0;
        if (p) { memmove(left, p + 1, llen - n); llen -= n + 1; } else { left[0] = 0; llen = 0; }
        if (n == 0 || strcmp(next, ".") == 0) continue;
        if (strcmp(next, "..") == 0) {
            if (rlen > 1) { out[rlen - 1 > 0 ? rlen - 1 : 0] = out[rlen - 1]; while (rlen > 1 && out[rlen - 1] != '/') rlen--; if (rlen > 1) rlen--; out[rlen] = 0; if (rlen == 0) { out[0] = '/'; out[1] = 0; rlen = 1; } }
            continue;
        }
        if (rlen + 1 + n >= PATH_MAX) { errno = ENAMETOOLONG; goto fail; }
        if (out[rlen - 1] != '/') out[rlen++] = '/';
        memcpy(out + rlen, next, n + 1); rlen += n;
        struct stat st;
        if (lstat(out, &st) != 0) goto fail;
        if (S_ISLNK(st.st_mode)) {
            if (++symlinks > 32) { errno = ELOOP; goto fail; }
            ssize_t m = readlink(out, link, sizeof link - 1);
            if (m < 0) goto fail;
            link[m] = 0;
            if (link[0] == '/') { out[0] = '/'; out[1] = 0; rlen = 1; }
            else { while (rlen > 1 && out[rlen - 1] != '/') rlen--; if (rlen > 1) rlen--; out[rlen] = 0; if (rlen == 0) { out[0] = '/'; rlen = 1; out[1] = 0; } }
            if (llen > 0) { if ((size_t)m + 1 + llen >= PATH_MAX) { errno = ENAMETOOLONG; goto fail; } strcat(link, "/"); strcat(link, left); }
            strcpy(left, link); llen = strlen(left);
            if (left[0] == '/') { memmove(left, left + 1, llen); llen--; }
        }
    }
    if (rlen > 1 && out[rlen - 1] == '/') out[--rlen] = 0;
    return out;
fail:
    if (!resolved) free(out);
    return 0;
}
/* The SDK's <stdlib.h> declares realpath with the $DARWIN_EXTSN suffix, so this definition exports _realpath$DARWIN_EXTSN;
 * the plain _realpath (what code built without the extension, or dlsym, binds) is the same function. */
char *realpath(const char *path, char *resolved) { return flo_rp(path, resolved); }
char *flo_realpath_plain(const char *path, char *resolved) __asm__("_realpath");
char *flo_realpath_plain(const char *path, char *resolved) { return flo_rp(path, resolved); }
