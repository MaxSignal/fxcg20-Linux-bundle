/* lwIP port: Linux user space with musl (or the host's libc) */
#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <endian.h>
#include <stdio.h>
#include <stdlib.h>

#define LWIP_PLATFORM_DIAG(x)	do { printf x; } while (0)
#define LWIP_PLATFORM_ASSERT(x)	do { fprintf(stderr, "lwip: %s\n", x); abort(); } while (0)
#define LWIP_RAND()		((u32_t)random())

#endif
