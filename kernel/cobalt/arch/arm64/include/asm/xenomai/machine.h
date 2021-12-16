/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2015 Dmitriy Cherkasov <dmitriy@mperpetuo.com>
 */

#ifndef _COBALT_ARM64_MACHINE_H
#define _COBALT_ARM64_MACHINE_H

#include <linux/version.h>
#include <asm/byteorder.h>
#include <cobalt/kernel/assert.h>

/* D-side always behaves as PIPT on AArch64 (see arch/arm64/include/asm/cachetype.h) */
#define xnarch_cache_aliasing() 0

static inline __attribute_const__ unsigned long ffnz(unsigned long ul)
{
	int __r;

	/* zero input is not valid */
	XENO_WARN_ON(COBALT, ul == 0);

	__asm__ ("rbit\t%0, %1\n"
	         "clz\t%0, %0\n"
	        : "=r" (__r) : "r"(ul) : "cc");

	return __r;
}

#include <asm-generic/xenomai/machine.h>

#endif /* !_COBALT_ARM64_MACHINE_H */
