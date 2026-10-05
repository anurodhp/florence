/* SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel) */
/* More libSystem/compiler-rt/libmalloc entries WebKit 2.54 links against and the iokit port's libSystem does not
 * export (docs/HANDOFF.md, urgent DarwinOS bugs). Each is the documented behaviour over what the port does export.
 * Debt: they belong in libsystem_c / libsystem_malloc / compiler-rt; delete them as they land. */
#include <errno.h>
#include <mach-o/loader.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- compiler-rt: 128-bit integer <-> double, signed remainder (fixdfti.c, floattidf.c, modti3.c) ------------- */
typedef __int128 flo_s128;
typedef unsigned __int128 flo_u128;
extern flo_u128 __udivti3(flo_u128, flo_u128);
extern flo_u128 __umodti3(flo_u128, flo_u128);

flo_s128 __fixdfti(double a) {            /* truncates toward zero, saturates */
    if (a != a) return 0;
    uint64_t bits; memcpy(&bits, &a, 8);
    int neg = (int)(bits >> 63), e = (int)((bits >> 52) & 0x7ff);
    if (e < 1023) return 0;                                  /* |a| < 1 */
    if (e >= 1023 + 127) return neg ? (flo_s128)((flo_u128)1 << 127) : (flo_s128)(((flo_u128)1 << 127) - 1);
    flo_u128 mant = ((flo_u128)1 << 52) | (bits & (((uint64_t)1 << 52) - 1));
    int sh = e - 1075;                                       /* value = mant * 2^sh */
    flo_u128 r = sh >= 0 ? mant << sh : mant >> -sh;
    return neg ? -(flo_s128)r : (flo_s128)r;
}
double __floattidf(flo_s128 a) {
    int neg = a < 0; flo_u128 u = neg ? -(flo_u128)a : (flo_u128)a;
    double d = (double)(uint64_t)(u >> 64) * 18446744073709551616.0 + (double)(uint64_t)u;
    return neg ? -d : d;
}
flo_s128 __modti3(flo_s128 a, flo_s128 b) {
    flo_u128 ua = a < 0 ? -(flo_u128)a : (flo_u128)a, ub = b < 0 ? -(flo_u128)b : (flo_u128)b;
    flo_u128 r = __umodti3(ua, ub);
    return a < 0 ? -(flo_s128)r : (flo_s128)r;
}

/* ---- libm: ___sincosf_stret returns { sin, cos } as an HFA in s0/s1 -------------------------------------------- */
float sinf(float); float cosf(float);
typedef struct { float sinval, cosval; } flo_float2;
flo_float2 __sincosf_stret(float x) { flo_float2 r = { sinf(x), cosf(x) }; return r; }

/* ---- execinfo: backtrace() by walking the arm64 frame-pointer chain (x29: [fp] = caller's fp, [fp+8] = return pc) */
int backtrace(void **buf, int size) {
    uintptr_t *fp = (uintptr_t *)__builtin_frame_address(0);
    int n = 0;
    while (fp && ((uintptr_t)fp & 7) == 0 && n < size) {
        uintptr_t ret = fp[1], next = fp[0];
        if (!ret) break;
        buf[n++] = (void *)ret;
        if (next <= (uintptr_t)fp) break;      /* frames grow toward higher addresses; stop on a bad chain */
        fp = (uintptr_t *)next;
    }
    return n;
}
char **backtrace_symbols(void *const *buf, int n) {    /* "0xaddr": no symbol lookup here (dladdr is not wired), one malloc block */
    if (n <= 0) return 0;
    char **out = malloc((size_t)n * (sizeof(char *) + 24));
    if (!out) return 0;
    char *s = (char *)(out + n);
    for (int i = 0; i < n; i++) { out[i] = s; s += snprintf(s, 24, "0x%lx", (unsigned long)(uintptr_t)buf[i]) + 1; }
    return out;
}

/* ---- libmalloc zones: WTF/bmalloc's SystemHeap uses a private zone; this port's libsystem_malloc has none, so every
 * zone is the default heap (malloc/free). malloc_zone_t stays opaque. -------------------------------------------- */
typedef struct flo_zone { int unused; } flo_zone_t;
static flo_zone_t flo_default_zone;
flo_zone_t *malloc_default_zone(void) { return &flo_default_zone; }
flo_zone_t *malloc_create_zone(size_t start_size, unsigned flags) { (void)start_size; (void)flags; return &flo_default_zone; }
void malloc_set_zone_name(flo_zone_t *z, const char *name) { (void)z; (void)name; }
void *malloc_zone_malloc(flo_zone_t *z, size_t n) { (void)z; return malloc(n); }
void *malloc_zone_calloc(flo_zone_t *z, size_t c, size_t n) { (void)z; return calloc(c, n); }
void *malloc_zone_realloc(flo_zone_t *z, void *p, size_t n) { (void)z; return realloc(p, n); }
void *malloc_zone_valloc(flo_zone_t *z, size_t n) { (void)z; void *p = 0; return posix_memalign(&p, 4096, n) == 0 ? p : 0; }
void *malloc_zone_memalign(flo_zone_t *z, size_t a, size_t n) { (void)z; void *p = 0; return posix_memalign(&p, a, n) == 0 ? p : 0; }
void malloc_zone_free(flo_zone_t *z, void *p) { (void)z; free(p); }
size_t malloc_zone_pressure_relief(flo_zone_t *z, size_t goal) { (void)z; (void)goal; return 0; }
void malloc_zone_print(flo_zone_t *z, int verbose) { (void)z; (void)verbose; }

/* ---- string.h: memset_pattern4/8/16 (Darwin extension): fill len bytes with the repeated pattern --------------- */
static void flo_pattern(void *b, const void *pat, size_t len, size_t psize) {
    unsigned char *d = b; const unsigned char *p = pat;
    for (size_t i = 0; i < len; i++) d[i] = p[i % psize];
}
void memset_pattern4(void *b, const void *p, size_t len) { flo_pattern(b, p, len, 4); }
void memset_pattern8(void *b, const void *p, size_t len) { flo_pattern(b, p, len, 8); }
void memset_pattern16(void *b, const void *p, size_t len) { flo_pattern(b, p, len, 16); }

/* ---- CommonCrypto: CCRandomGenerateBytes (WTF::RandomDevice's Darwin branch). Returns kCCSuccess (0) or kCCUnspecifiedError (-4304) */
int getentropy(void *, size_t);
int CCRandomGenerateBytes(void *bytes, size_t count) {
    unsigned char *p = bytes;
    while (count) { size_t n = count > 256 ? 256 : count; if (getentropy(p, n) != 0) return -4304; p += n; count -= n; }
    return 0;
}

/* ---- <mach-o/getsect.h> getsegmentdata: the segment's address (vmaddr + slide) and size in a loaded image.
 * Only a stub exports it in this port (libopenpam). Slide = header address - __TEXT's vmaddr. -------------------- */
uint8_t *getsegmentdata(const struct mach_header_64 *mh, const char *segname, unsigned long *size) {
    const struct load_command *lc = (const struct load_command *)(mh + 1);
    intptr_t slide = 0; int haveSlide = 0;
    for (uint32_t i = 0; i < mh->ncmds; i++, lc = (const struct load_command *)((const char *)lc + lc->cmdsize))
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
            if (strncmp(sg->segname, "__TEXT", 16) == 0) { slide = (intptr_t)mh - (intptr_t)sg->vmaddr; haveSlide = 1; break; }
        }
    if (!haveSlide) return 0;
    lc = (const struct load_command *)(mh + 1);
    for (uint32_t i = 0; i < mh->ncmds; i++, lc = (const struct load_command *)((const char *)lc + lc->cmdsize))
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
            if (strncmp(sg->segname, segname, 16) == 0) { if (size) *size = (unsigned long)sg->vmsize; return (uint8_t *)(sg->vmaddr + slide); }
        }
    return 0;
}

/* ---- libcache: sys_icache_invalidate / sys_dcache_flush (arm64) -------------------------------------------------
 * JavaScriptCore's ARM64 assembler (LLInt and the JIT) makes freshly written code visible to the instruction fetch with
 * these. The architecture's sequence: clean the data cache to the point of unification, invalidate the instruction cache,
 * with barriers, line by line (CTR_EL0 gives the line sizes). */
#if defined(__aarch64__)
void sys_dcache_flush(void *start, size_t len)
{
    uint64_t ctr;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    uintptr_t line = (uintptr_t)4 << ((ctr >> 16) & 0xf);
    uintptr_t a = (uintptr_t)start & ~(line - 1), end = (uintptr_t)start + len;
    for (; a < end; a += line)
        __asm__ volatile("dc cvau, %0" : : "r"(a) : "memory");
    __asm__ volatile("dsb ish" : : : "memory");
}

void sys_icache_invalidate(void *start, size_t len)
{
    uint64_t ctr;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    uintptr_t dline = (uintptr_t)4 << ((ctr >> 16) & 0xf), iline = (uintptr_t)4 << (ctr & 0xf);
    uintptr_t a = (uintptr_t)start & ~(dline - 1), end = (uintptr_t)start + len;
    for (; a < end; a += dline)
        __asm__ volatile("dc cvau, %0" : : "r"(a) : "memory");
    __asm__ volatile("dsb ish" : : : "memory");
    for (a = (uintptr_t)start & ~(iline - 1); a < end; a += iline)
        __asm__ volatile("ic ivau, %0" : : "r"(a) : "memory");
    __asm__ volatile("dsb ish\n\tisb" : : : "memory");
}
#endif
