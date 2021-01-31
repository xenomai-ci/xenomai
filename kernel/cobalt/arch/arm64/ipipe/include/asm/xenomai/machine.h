/**
 *   Copyright &copy; 2002-2004 Philippe Gerum.
 *
 *   ARM port
 *     Copyright (C) 2005 Stelian Pop
 *
 *   Xenomai is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License as
 *   published by the Free Software Foundation, Inc., 675 Mass Ave,
 *   Cambridge MA 02139, USA; either version 2 of the License, or (at
 *   your option) any later version.
 *
 *   Xenomai is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Xenomai; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *   02111-1307, USA.
 */
#ifndef _COBALT_ARM64_ASM_MACHINE_H
#define _COBALT_ARM64_ASM_MACHINE_H

#include <linux/version.h>
#include <asm/byteorder.h>

#define XNARCH_HOST_TICK_IRQ __ipipe_hrtimer_irq

#include <asm/barrier.h>
#include <asm/compiler.h>
#include <asm/cmpxchg.h>
#include <asm/switch_to.h>
#include <asm/system_misc.h>
#include <asm/timex.h>
#include <asm/processor.h>
#include <asm/ipipe.h>
#include <asm/cacheflush.h>
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

#endif /* !_COBALT_ARM64_ASM_MACHINE_H */
