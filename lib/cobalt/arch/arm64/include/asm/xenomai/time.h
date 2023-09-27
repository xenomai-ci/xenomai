/*
 * Copyright (C) 2021 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef _LIB_COBALT_ARM64_TIME_H
#define _LIB_COBALT_ARM64_TIME_H

#define COBALT_VDSO_VERSION	"LINUX_2.6.39"

#ifdef __USE_TIME_BITS64
#define COBALT_VDSO_GETTIME	"__vdso_clock_gettime64"
#else
#define COBALT_VDSO_GETTIME	"__kernel_clock_gettime"
#endif

#endif /* !_LIB_COBALT_ARM64_TIME_H */
