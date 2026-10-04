/* SPDX-License-Identifier: APSL-2.0 (interface declarations modelled on Apple's XNU / macOS headers, which are APSL-2.0; see LICENSE) */
/* The Xcode 12 iPhoneOS SDK has no <sys/random.h>; getentropy is exported by the port's libsystem_c.dylib
 * (libgcrypt's rndgetentropy.c and other code look for it here). Prototype as in macOS 10.12's header. */
#ifndef FLO_SYS_RANDOM_H
#define FLO_SYS_RANDOM_H
#include <stddef.h>
int getentropy(void *buffer, size_t size);
#endif
