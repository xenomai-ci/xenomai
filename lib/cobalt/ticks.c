/*
 * Copyright (C) 2013 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#include <cobalt/arith.h>
#include <cobalt/ticks.h>
#include <asm/xenomai/tsc.h>
#include "internal.h"

unsigned long long __cobalt_tsc_clockfreq;

#ifdef XNARCH_HAVE_LLMULSHFT

static unsigned int tsc_scale, tsc_shift;

#ifdef XNARCH_HAVE_NODIV_LLIMD

static struct xnarch_u32frac tsc_frac;
static struct xnarch_u32frac bln_frac;

unsigned long long cobalt_divrem_billion(unsigned long long value,
					 unsigned long *rem)
{
	unsigned long long q;
	unsigned r;

	q = xnarch_nodiv_ullimd(value, bln_frac.frac, bln_frac.integ);
	r = value - q * 1000000000;
	if (r >= 1000000000) {
		++q;
		r -= 1000000000;
	}
	*rem = r;
	return q;
}

xnsticks_t __cobalt_ns_to_tsc(xnsticks_t ns)
{
	return xnarch_nodiv_llimd(ns, tsc_frac.frac, tsc_frac.integ);
}

#else /* !XNARCH_HAVE_NODIV_LLIMD */

xnsticks_t __cobalt_ns_to_tsc(xnsticks_t ns)
{
	return xnarch_llimd(ns, 1 << tsc_shift, tsc_scale);
}

#endif /* !XNARCH_HAVE_NODIV_LLIMD */

xnsticks_t __cobalt_tsc_to_ns(xnsticks_t ticks)
{
	return xnarch_llmulshft(ticks, tsc_scale, tsc_shift);
}

xnsticks_t __cobalt_tsc_to_ns_rounded(xnsticks_t ticks)
{
	unsigned int shift = tsc_shift - 1;
	return (xnarch_llmulshft(ticks, tsc_scale, shift) + 1) / 2;
}

#else  /* !XNARCH_HAVE_LLMULSHFT */

xnsticks_t __cobalt_tsc_to_ns(xnsticks_t ticks)
{
	return xnarch_llimd(ticks, 1000000000, __cobalt_tsc_clockfreq);
}

xnsticks_t __cobalt_tsc_to_ns_rounded(xnsticks_t ticks)
{
	return (xnarch_llimd(ticks, 1000000000, __cobalt_tsc_clockfreq/2) + 1) / 2;
}

xnsticks_t __cobalt_ns_to_tsc(xnsticks_t ns)
{
	return xnarch_llimd(ns, __cobalt_tsc_clockfreq, 1000000000);
}
#endif /* !XNARCH_HAVE_LLMULSHFT */

#ifndef XNARCH_HAVE_NODIV_LLIMD
unsigned long long cobalt_divrem_billion(unsigned long long value,
					 unsigned long *rem)
{
	return xnarch_ulldiv(value, 1000000000, rem);

}
#endif /* !XNARCH_HAVE_NODIV_LLIMD */

void cobalt_ticks_init(unsigned long long freq)
{
	__cobalt_tsc_clockfreq = freq;
#ifdef XNARCH_HAVE_LLMULSHFT
	if (freq) {
		xnarch_init_llmulshft(1000000000, freq, &tsc_scale, &tsc_shift);
#ifdef XNARCH_HAVE_NODIV_LLIMD
		xnarch_init_u32frac(&tsc_frac, 1 << tsc_shift, tsc_scale);
#endif
	}
#endif
#ifdef XNARCH_HAVE_NODIV_LLIMD
	xnarch_init_u32frac(&bln_frac, 1, 1000000000);
#endif
}
