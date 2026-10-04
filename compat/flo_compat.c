/* SPDX-License-Identifier: GPL-2.0-only (Copyright (c) 2026 Anurodh Pokharel) */
/* libflocompat: symbols the iokit port's libSystem does not export yet but its kernel implements. Each entry
 * names the kernel source it rests on. Debt: they belong in the iokit port's libsystem_kernel wrapper set;
 * when they land there, delete the entry here. scripts/build_compat.sh runs the bind audit. */

/* int getfsstat(struct statfs *buf, int bufsize, int flags)
 * third_party/xnu-7195/bsd/kern/syscalls.master:527: 347 is getfsstat64, the struct statfs with 64-bit inode
 * numbers that the SDK's struct statfs is (:284 marks the old 193 slot nosys).
 * Raw arm64 BSD syscall: number in x16, svc #0x80, carry set = failure with errno in x0. */
__asm__(
    ".text\n.align 2\n.globl _getfsstat\n_getfsstat:\n"
    "    mov x16, #347\n"
    "    svc #0x80\n"
    "    b.cc 1f\n"
    "    stp x29, x30, [sp, #-32]!\n"
    "    mov x29, sp\n"
    "    str x0, [sp, #16]\n"
    "    bl ___error\n"
    "    ldr x1, [sp, #16]\n"
    "    str w1, [x0]\n"
    "    mov x0, #-1\n"
    "    ldp x29, x30, [sp], #32\n"
    "1:  ret\n");

/* unsigned __int128 division helpers (compiler-rt's udivti3.c / umodti3.c). Xcode 12's libclang_rt.ios.a defines
 * neither (nm -arch arm64 shows both undefined) and libSystem does not export them; GLib's gmain.c needs
 * ___udivti3 for its microsecond time arithmetic. Plain shift-subtract: correct, and used on cold paths only. */
typedef unsigned __int128 flo_u128;
static flo_u128 flo_udivmod128(flo_u128 n, flo_u128 d, flo_u128 *rem) {
    flo_u128 q = 0, r = 0;
    if (d == 0) { q = 1 / (unsigned)d; }              /* same trap as the hardware divide-by-zero */
    for (int i = 127; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) { r -= d; q |= (flo_u128)1 << i; }
    }
    if (rem) *rem = r;
    return q;
}
flo_u128 __udivti3(flo_u128 n, flo_u128 d) { return flo_udivmod128(n, d, 0); }
flo_u128 __umodti3(flo_u128 n, flo_u128 d) { flo_u128 r; flo_udivmod128(n, d, &r); return r; }

/* __int128 signed division (compiler-rt divti3.c), same gap as __udivti3 above. */
typedef __int128 flo_s128;
flo_s128 __divti3(flo_s128 a, flo_s128 b) {
    int neg = (a < 0) ^ (b < 0);
    flo_u128 q = flo_udivmod128(a < 0 ? -(flo_u128)a : (flo_u128)a, b < 0 ? -(flo_u128)b : (flo_u128)b, 0);
    return neg ? -(flo_s128)q : (flo_s128)q;
}

/* void *aligned_alloc(size_t alignment, size_t size): C11, absent from libsystem_malloc (only posix_memalign
 * is exported). libc++'s operator new(size_t, align_val_t) needs it. aligned_alloc's contract is posix_memalign's
 * with the error returned as NULL + errno. */
extern int posix_memalign(void **, unsigned long, unsigned long);
void *aligned_alloc(unsigned long alignment, unsigned long size) {
    void *p = 0;
    return posix_memalign(&p, alignment, size) == 0 ? p : 0;
}

/* close$NOCANCEL, read$NOCANCEL, write$NOCANCEL: the cancellation-point-free variants Darwin's libSystem exports
 * (GLib 2.78 gspawn.c and the SDK headers' __DARWIN_ALIAS_C bind to them). libsystem_c exports only the plain ones;
 * this port has no pthread cancellation, so the plain call is the same call. */
int close(int); long read(int, void *, unsigned long); long write(int, const void *, unsigned long);
int flo_close_nocancel(int fd) __asm__("_close$NOCANCEL");
long flo_read_nocancel(int fd, void *b, unsigned long n) __asm__("_read$NOCANCEL");
long flo_write_nocancel(int fd, const void *b, unsigned long n) __asm__("_write$NOCANCEL");
int flo_close_nocancel(int fd) { return close(fd); }
long flo_read_nocancel(int fd, void *b, unsigned long n) { return read(fd, b, n); }
long flo_write_nocancel(int fd, const void *b, unsigned long n) { return write(fd, b, n); }
